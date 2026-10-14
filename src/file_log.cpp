#include "mafia/file_log.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace mafia {

namespace {

std::string causeList(const std::vector<DeathCause>& causes) {
    std::string text;
    for (DeathCause cause : causes) {
        text += (text.empty() ? "" : ", ") + std::string(causeName(cause));
    }
    return text;
}

// Ширина в символах, а не в байтах: русские буквы в UTF-8 занимают по два байта.
std::size_t displayWidth(const std::string& text) {
    return static_cast<std::size_t>(
        std::ranges::count_if(text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

std::string pad(const std::string& text, std::size_t width) {
    const std::size_t current = displayWidth(text);
    return current >= width ? text + " " : text + std::string(width - current, ' ');
}

std::string trimRight(std::string text) {
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

bool executedBy(const std::vector<DeathCause>& causes) {
    return causes.size() == 1 && causes.front() == DeathCause::Execution;
}

}  // namespace

FileLog::FileLog(std::filesystem::path directory, Warn warn)
    : directory_(std::move(directory)), warn_(std::move(warn)) {
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) {
        disable("не удалось создать каталог «" + directory_.string() + "»: " + error.message());
        return;
    }
    if (!std::filesystem::is_directory(directory_, error)) {
        disable("«" + directory_.string() + "» не является каталогом");
        return;
    }
    // Файлы прошлой партии в том же каталоге удаляем, чтобы не смешать раунды.
    std::vector<std::filesystem::path> stale;
    for (std::filesystem::directory_iterator it(directory_, error), end; !error && it != end; it.increment(error)) {
        const std::string file = it->path().filename().string();
        if ((file.starts_with("round_") && file.ends_with(".txt")) || file == "summary.txt") {
            stale.push_back(it->path());
        }
    }
    for (const auto& path : stale) {
        std::filesystem::remove(path, error);
    }
}

// ------------------------------------------------------------------ события

void FileLog::gameStarted(const GameSetupInfo& setup) {
    setup_ = setup;
    stats_.assign(setup.roles.size() + 1, PlayerStats{});
}

void FileLog::dayStarted(int day, const std::vector<int>& alive) {
    openRound(day);
    write("Раунд " + std::to_string(day) + ": день " + std::to_string(day) + " и ночь " + std::to_string(day));
    std::string names;
    for (int player : alive) {
        names += (names.empty() ? "" : ", ") + name(player);
    }
    write("Живы: " + names);
    write("");
    write("== День " + std::to_string(day) + ": обсуждение ==");
}

void FileLog::statement(int, int speaker, const Response& response, const std::string& text) {
    ++stats(speaker).statements;
    std::string line = name(speaker) + ": «" + text + "»";
    if (response.action == ActionType::Accuse) {
        ++stats(speaker).accusationsMade;
        ++stats(response.target).timesAccused;
        line += " — обвиняет " + name(response.target);
    } else {
        line += " — заявляет о невиновности";
    }
    if (!response.reason.empty()) {
        line += "; мотив: " + response.reason;
    }
    write(line);
}

void FileLog::voteCast(int day, int voter, const Response& response) {
    if (voteHeaderDay_ != day) {
        voteHeaderDay_ = day;
        write("");
        write("== День " + std::to_string(day) + ": голосование ==");
    }
    if (response.action == ActionType::Abstain) {
        write(name(voter) + " воздерживается" +
              (response.message.empty() ? "" : "; сказал: «" + response.message + "»") +
              (response.reason.empty() ? "" : "; мотив: " + response.reason));
        return;
    }
    ++stats(voter).votesCast;
    ++stats(response.target).votesReceived;
    if (teamOf(roleOf(response.target)) == Team::Mafia) {
        ++stats(voter).votesAgainstMafia;
    }
    write(name(voter) + " → " + name(response.target) +
          (response.message.empty() ? "" : "; сказал: «" + response.message + "»") +
          (response.reason.empty() ? "" : "; мотив: " + response.reason));
}

void FileLog::votingFinished(int, const std::vector<VoteRow>& rows, int executed, bool coinToss) {
    write("Итоги голосования:");
    for (const auto& [target, voters] : rows) {
        std::string ids;
        for (int voter : voters) {
            ids += (ids.empty() ? "" : ", ") + std::to_string(voter);
        }
        write("  " + name(target) + " — " + std::to_string(voters.size()) + " (голосовали: " + ids + ")");
    }
    if (executed == 0) {
        write("Решение: все воздержались, никто не выбывает");
        return;
    }
    write("Решение: выбывает " + name(executed) + (coinToss ? " (голоса разделились поровну, решила монетка)" : ""));
}

void FileLog::nightStarted(int night) {
    write("");
    write("== Ночь " + std::to_string(night) + " ==");
}

void FileLog::nightAction(int, int actor, const Response& response) {
    ++stats(actor).nightActions;
    std::string text;
    switch (response.action) {
        case ActionType::Kill:
            text = teamOf(roleOf(actor)) == Team::Mafia ? "предлагает убить " : "убивает ";
            text += name(response.target);
            break;
        case ActionType::Heal:
            text = "лечит " + name(response.target);
            break;
        case ActionType::Check:
            text = "проверяет " + name(response.target);
            break;
        case ActionType::Shoot:
            text = "стреляет в " + name(response.target);
            break;
        case ActionType::Guard:
            text = "охраняет " + name(response.target);
            break;
        case ActionType::Compare:
            text = "сравнивает " + name(response.target) + " и " + name(response.secondTarget);
            break;
        default:
            text = "пропускает ход";
            break;
    }
    write(name(actor) + ": " + text + (response.reason.empty() ? "" : "; мотив: " + response.reason));
}

void FileLog::nightResolved(int, const NightOutcome& outcome) {
    write("Итоги ночи:");
    if (outcome.mafiaVictim) {
        write("  решение мафии: " + name(*outcome.mafiaVictim) + ", стрелял " + name(outcome.mafiaExecutor));
    }
    for (const auto& [target, causes] : outcome.attacks) {
        write("  покушение на " + name(target) + ": " + causeList(causes));
    }
    for (const auto& [target, causes] : outcome.healed) {
        ++saves_;
        ++stats(target).timesSaved;
        write("  доктор спас " + name(target) + " (" + causeList(causes) + ")");
    }
    for (const auto& [target, causes] : outcome.guarded) {
        ++saves_;
        ++stats(target).timesSaved;
        write("  телохранитель прикрыл " + name(target) + " (" + causeList(causes) + ")");
    }
    if (outcome.counterKilled) {
        write("  телохранитель ответил выстрелом: " + name(*outcome.counterKilled));
    }
    if (outcome.check) {
        write("  проверка комиссара: " + name(outcome.check->target) + " — " +
              (outcome.check->isMafia ? "мафия" : "не мафия"));
    }
    if (outcome.comparison) {
        write("  сравнение журналиста: " + name(outcome.comparison->first) + " и " +
              name(outcome.comparison->second) + " — " +
              (outcome.comparison->same ? "одинаковый статус" : "разный статус"));
    }
    std::string deaths;
    for (int player : outcome.deaths) {
        deaths += (deaths.empty() ? "" : ", ") + name(player);
    }
    write("  погибли: " + (deaths.empty() ? std::string("никто") : deaths));
}

void FileLog::playerEliminated(int day, int player, const std::vector<DeathCause>& causes) {
    PlayerStats& playerStats = stats(player);
    playerStats.deathDay = day;
    playerStats.deathCauses = causes;
    if (executedBy(causes)) {
        ++executions_;
    } else {
        ++nightDeaths_;
    }
    write("Выбыл: " + name(player) + " — " + causeList(causes));
}

void FileLog::gameEnded(Winner winner, int days) {
    write("");
    write("Игра окончена. Победитель: " + std::string(winnerName(winner)) + ".");
    closeRound();
    writeSummary(winner, days);
}

// ------------------------------------------------------------------- файлы

void FileLog::openRound(int round) {
    closeRound();
    if (!active_) {
        return;
    }
    char file[32];
    std::snprintf(file, sizeof(file), "round_%02d.txt", round);
    roundPath_ = directory_ / file;
    round_.open(roundPath_, std::ios::out | std::ios::trunc);
    if (!round_) {
        disable("не удалось открыть «" + roundPath_.string() + "» для записи");
    }
}

void FileLog::closeRound() {
    if (!round_.is_open()) {
        return;
    }
    round_.close();
    if (!round_) {
        disable("ошибка при записи «" + roundPath_.string() + "»");
    }
}

void FileLog::write(const std::string& line) {
    if (!active_ || !round_.is_open()) {
        return;
    }
    round_ << line << '\n';
    if (!round_) {
        disable("ошибка при записи «" + roundPath_.string() + "»");
    }
}

void FileLog::disable(const std::string& reason) {
    if (!active_) {
        return;
    }
    active_ = false;
    round_.close();
    if (warn_) {
        warn_("Файловые логи отключены: " + reason + ". Партия продолжается без них.");
    }
}

void FileLog::writeSummary(Winner winner, int days) {
    if (!active_) {
        return;
    }
    const std::filesystem::path path = directory_ / "summary.txt";
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        disable("не удалось открыть «" + path.string() + "» для записи");
        return;
    }

    out << "Итоги партии «Мафия»\n";
    out << "Сид: " << setup_.seed << "\n";
    out << "Игроков: " << setup_.players << "\n";
    out << "Объявления: " << (setup_.openAnnouncements ? "открытые" : "закрытые") << "\n";
    out << "Набор ролей: " << setup_.rolesSource << "\n";
    if (!setup_.skippedRoles.empty()) {
        out << "Пропущенные роли:\n";
        for (const auto& message : setup_.skippedRoles) {
            out << "  " << message << "\n";
        }
    }
    if (!setup_.aiAgents.empty()) {
        std::string agents;
        for (int player : setup_.aiAgents) {
            agents += (agents.empty() ? "" : ", ") + std::to_string(player);
        }
        out << "ИИ-агенты: игроки " << agents << " (модель " << setup_.aiModel << ")\n";
    }
    out << "Победитель: " << winnerName(winner) << "\n";
    out << "Сыграно дней: " << days << "\n";
    out << "Изгнано голосованием: " << executions_ << ", погибло ночью: " << nightDeaths_
        << ", спасений за ночь: " << saves_ << "\n\n";

    const std::vector<std::pair<std::string, std::size_t>> columns = {
        {"Игрок", 13},   {"Роль", 15},    {"Лагерь", 8},  {"Итог", 10},     {"Судьба", 34},
        {"Реплик", 7},   {"Обвинял", 8},  {"Обвинён", 8}, {"Голосов", 8},   {"Против мафии", 13},
        {"Получил", 8},  {"Ночью", 6},    {"Спасён", 6},
    };
    out << "Статистика по игрокам (Голосов — отдано, Получил — голосов против него, Ночью — ночных ходов)\n";
    std::string header;
    for (const auto& [title, width] : columns) {
        header += pad(title, width);
    }
    out << trimRight(header) << "\n";

    for (int player = 1; player <= setup_.players; ++player) {
        const Role role = roleOf(player);
        const Team team = teamOf(role);
        const PlayerStats& s = stats(player);
        const bool won = (winner == Winner::Town && team == Team::Town) ||
                         (winner == Winner::Mafia && team == Team::Mafia) ||
                         (winner == Winner::Maniac && team == Team::Maniac);
        std::string fate = "дожил до конца";
        if (s.deathDay != 0) {
            fate = executedBy(s.deathCauses) ? "изгнан в день " + std::to_string(s.deathDay)
                                             : "погиб в ночь " + std::to_string(s.deathDay - 1) + " (" +
                                                   causeList(s.deathCauses) + ")";
        }
        const std::vector<std::string> cells = {
            "Игрок " + std::to_string(player) + (player == setup_.humanId ? "*" : ""),
            std::string(roleName(role)),
            std::string(teamName(team)),
            won ? "победил" : "проиграл",
            fate,
            std::to_string(s.statements),
            std::to_string(s.accusationsMade),
            std::to_string(s.timesAccused),
            std::to_string(s.votesCast),
            std::to_string(s.votesAgainstMafia),
            std::to_string(s.votesReceived),
            std::to_string(s.nightActions),
            std::to_string(s.timesSaved),
        };
        std::string row;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            row += pad(cells[i], columns[i].second);
        }
        out << trimRight(row) << "\n";
    }
    if (setup_.humanId != 0) {
        out << "* — игрок-человек\n";
    }
    out.close();
    if (!out) {
        disable("ошибка при записи «" + path.string() + "»");
    }
}

// ----------------------------------------------------------------- утилиты

std::string FileLog::name(int player) const {
    return "Игрок " + std::to_string(player) + " [" + std::string(roleName(roleOf(player))) + "]";
}

FileLog::PlayerStats& FileLog::stats(int player) {
    const auto index = static_cast<std::size_t>(player);
    if (index >= stats_.size()) {
        stats_.resize(index + 1);
    }
    return stats_[index];
}

Role FileLog::roleOf(int player) const {
    const auto index = static_cast<std::size_t>(player - 1);
    return index < setup_.roles.size() ? setup_.roles[index] : Role::Civilian;
}

}  // namespace mafia
