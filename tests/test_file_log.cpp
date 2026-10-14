// Тесты файлового лога: файлы раундов, итоговая статистика и поведение при ошибках записи.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "mafia/file_log.hpp"
#include "mafia/night.hpp"
#include "test_framework.hpp"

namespace fs = std::filesystem;
using mafia::DeathCause;
using mafia::FileLog;
using mafia::Role;

namespace {

// Временный каталог, удаляемый в конце теста.
class TempDir {
public:
    TempDir() {
        std::random_device device;
        path_ = fs::temp_directory_path() / ("mafia_file_log_test_" + std::to_string(device()));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code error;
        fs::permissions(path_, fs::perms::owner_all, fs::perm_options::add, error);
        fs::remove_all(path_, error);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

std::string readFile(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool hasText(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// Строка таблицы, начинающаяся с prefix, с пробелами, схлопнутыми до одного.
std::string row(const std::string& table, const std::string& prefix) {
    std::istringstream lines(table);
    for (std::string line; std::getline(lines, line);) {
        if (line.starts_with(prefix + " ")) {
            std::string collapsed;
            for (char c : line) {
                if (c != ' ' || (!collapsed.empty() && collapsed.back() != ' ')) {
                    collapsed += c;
                }
            }
            return collapsed;
        }
    }
    return {};
}

// Позиция подстроки в символах (а не байтах UTF-8).
std::size_t column(const std::string& line, const std::string& part) {
    const std::string before = line.substr(0, line.find(part));
    return static_cast<std::size_t>(
        std::ranges::count_if(before, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

// Игроки: 1 — мафия, 2 — комиссар, 3 — доктор, 4 — телохранитель, 5 — мирный.
mafia::GameSetupInfo smallGame() {
    return mafia::GameSetupInfo{.players = 5,
                                .seed = 42,
                                .openAnnouncements = true,
                                .humanId = 0,
                                .roles = {Role::Mafia, Role::Commissar, Role::Doctor, Role::Bodyguard, Role::Civilian},
                                .skippedRoles = {"Роль «Журналист» пропущена: при 5 игроках для неё не хватило места."},
                                .rolesSource = "test.yaml"};
}

mafia::Response make(mafia::ActionType action, int target, std::string reason = {}) {
    mafia::Response response;
    response.action = action;
    response.target = target;
    response.reason = std::move(reason);
    return response;
}

// Короткая партия: день 1, ночь 1 (мафия убивает мирного), день 2 с изгнанием мафии.
void playSmallGame(mafia::GameEventSink& sink) {
    using mafia::ActionType;
    const auto setup = smallGame();
    sink.gameStarted(setup);

    sink.dayStarted(1, {1, 2, 3, 4, 5});
    sink.statement(1, 1, make(ActionType::ClaimInnocent, 0, "маскируется"), "Я мирный житель.");
    sink.statement(1, 2, make(ActionType::Accuse, 1), "Мне кажется, Игрок 1 — мафия.");

    sink.nightStarted(1);
    mafia::NightMoves moves;
    moves.mafia = {{1, 5}};
    moves.commissar = mafia::NightMoves::Move{2, 1};
    moves.doctor = mafia::NightMoves::Move{3, 3};
    moves.bodyguard = mafia::NightMoves::Move{4, 2};
    sink.nightAction(1, 1, make(ActionType::Kill, 5, "угроза 0.0"));
    sink.nightAction(1, 2, make(ActionType::Check, 1));
    sink.nightAction(1, 3, make(ActionType::Heal, 3));
    sink.nightAction(1, 4, make(ActionType::Guard, 2));
    const auto outcome = mafia::resolveNight(moves, setup.roles);
    sink.nightResolved(1, outcome);
    for (int victim : outcome.deaths) {
        sink.playerEliminated(2, victim, outcome.attacks.at(victim));
    }

    sink.dayStarted(2, {1, 2, 3, 4});
    for (int voter : {2, 3, 4}) {
        sink.voteCast(2, voter, make(ActionType::Vote, 1, "подозрение"));
    }
    sink.voteCast(2, 1, make(ActionType::Vote, 2));
    sink.votingFinished(2, {mafia::VoteRow{1, {2, 3, 4}}, mafia::VoteRow{2, {1}}}, 1, false);
    sink.playerEliminated(2, 1, {DeathCause::Execution});
    sink.gameEnded(mafia::Winner::Town, 2);
}

}  // namespace

TEST(WritesRoundFilesAndSummary) {
    TempDir temp;
    std::vector<std::string> warnings;
    const fs::path directory = temp.path() / "nested" / "game_42";  // каталоги создаются сами
    FileLog log(directory, [&warnings](const std::string& warning) { warnings.push_back(warning); });
    CHECK(log.active());
    playSmallGame(log);

    CHECK(warnings.empty());
    CHECK(log.active());
    CHECK(fs::is_regular_file(directory / "round_01.txt"));
    CHECK(fs::is_regular_file(directory / "round_02.txt"));
    CHECK(!fs::exists(directory / "round_03.txt"));

    const std::string round1 = readFile(directory / "round_01.txt");
    CHECK(hasText(round1, "Раунд 1: день 1 и ночь 1"));
    CHECK(hasText(round1, "Игрок 1 [Мафия]: «Я мирный житель.» — заявляет о невиновности; мотив: маскируется"));
    CHECK(hasText(round1, "— обвиняет Игрок 1 [Мафия]"));
    CHECK(hasText(round1, "== Ночь 1 =="));
    CHECK(hasText(round1, "Игрок 1 [Мафия]: предлагает убить Игрок 5 [Мирный житель]; мотив: угроза 0.0"));
    CHECK(hasText(round1, "Игрок 4 [Телохранитель]: охраняет Игрок 2 [Комиссар]"));
    CHECK(hasText(round1, "проверка комиссара: Игрок 1 [Мафия] — мафия"));
    CHECK(hasText(round1, "погибли: Игрок 5 [Мирный житель]"));
    CHECK(hasText(round1, "Выбыл: Игрок 5 [Мирный житель] — мафия"));  // ночная смерть — в файле своей ночи

    const std::string round2 = readFile(directory / "round_02.txt");
    CHECK(hasText(round2, "== День 2: голосование =="));
    CHECK(hasText(round2, "Игрок 2 [Комиссар] → Игрок 1 [Мафия]; мотив: подозрение"));
    CHECK(hasText(round2, "Игрок 1 [Мафия] — 3 (голосовали: 2, 3, 4)"));
    CHECK(hasText(round2, "Решение: выбывает Игрок 1 [Мафия]"));
    CHECK(hasText(round2, "Игра окончена. Победитель: мирные жители."));

    const std::string summary = readFile(directory / "summary.txt");
    CHECK(hasText(summary, "Сид: 42"));
    CHECK(hasText(summary, "Набор ролей: test.yaml"));
    CHECK(hasText(summary, "Роль «Журналист» пропущена"));
    CHECK(hasText(summary, "Победитель: мирные жители"));
    CHECK(hasText(summary, "Изгнано голосованием: 1, погибло ночью: 1"));
    // Колонки: игрок, роль, лагерь, итог, судьба, реплик, обвинял, обвинён, голосов,
    // против мафии, получил, ночью, спасён.
    CHECK_EQ(row(summary, "Игрок 1"), std::string("Игрок 1 Мафия мафия проиграл изгнан в день 2 1 0 1 1 0 3 1 0"));
    CHECK_EQ(row(summary, "Игрок 2"), std::string("Игрок 2 Комиссар мирные победил дожил до конца 1 1 0 1 1 1 1 0"));
    CHECK_EQ(row(summary, "Игрок 5"), std::string("Игрок 5 Мирный житель мирные победил погиб в ночь 1 (мафия) 0 0 0 0 0 0 0 0"));

    // Таблица выровнена по символам, несмотря на кириллицу в UTF-8.
    std::istringstream lines(summary);
    std::string header;
    std::string line1;
    for (std::string line; std::getline(lines, line);) {
        if (line.starts_with("Игрок ") && line.find("Роль") != std::string::npos) {
            header = line;
        } else if (line.starts_with("Игрок 1 ")) {
            line1 = line;
        }
    }
    CHECK_EQ(column(header, "Роль"), column(line1, "Мафия"));
    CHECK_EQ(column(header, "Судьба"), column(line1, "изгнан"));
}

TEST(RemovesFilesOfPreviousGame) {
    TempDir temp;
    std::ofstream(temp.path() / "round_07.txt") << "старый раунд";
    std::ofstream(temp.path() / "notes.txt") << "чужой файл";
    FileLog log(temp.path(), {});
    CHECK(!fs::exists(temp.path() / "round_07.txt"));
    CHECK(fs::exists(temp.path() / "notes.txt"));
}

TEST(DisablesItselfWhenDirectoryCannotBeCreated) {
    TempDir temp;
    std::ofstream(temp.path() / "blocker") << "это файл, а не каталог";
    std::vector<std::string> warnings;
    FileLog log(temp.path() / "blocker" / "logs", [&warnings](const std::string& warning) { warnings.push_back(warning); });
    CHECK(!log.active());
    CHECK_EQ(warnings.size(), std::size_t{1});
    CHECK(hasText(warnings.front(), "Файловые логи отключены"));
    CHECK(hasText(warnings.front(), "Партия продолжается без них"));

    playSmallGame(log);  // события после отказа просто игнорируются
    CHECK_EQ(warnings.size(), std::size_t{1});
}

TEST(DisablesItselfWhenFilesCannotBeWritten) {
    TempDir temp;
    const fs::path directory = temp.path() / "readonly";
    fs::create_directories(directory);
    fs::permissions(directory, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
    if (std::ofstream(directory / "probe.txt")) {
        std::cout << "  (каталог доступен на запись — например, под root; проверка пропущена)\n";
        return;
    }
    std::vector<std::string> warnings;
    FileLog log(directory, [&warnings](const std::string& warning) { warnings.push_back(warning); });
    CHECK(log.active());  // каталог существует — отказ проявится при первой записи
    playSmallGame(log);
    CHECK(!log.active());
    CHECK_EQ(warnings.size(), std::size_t{1});
    CHECK(hasText(warnings.front(), "round_01.txt"));
}

MAFIA_TEST_MAIN()
