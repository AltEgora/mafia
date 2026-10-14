#pragma once

// Логирование партии в файлы (std::filesystem):
//   <каталог>/round_01.txt, round_02.txt, …  — раунд k: день k и следующая ночь k;
//   <каталог>/summary.txt                     — итог партии и статистика по игрокам.
// Каталог создаётся автоматически. При любой ошибке записи лог выводит одно
// предупреждение и отключается — партия продолжается без файлов.

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "mafia/game_events.hpp"

namespace mafia {

class FileLog final : public GameEventSink {
public:
    using Warn = std::function<void(const std::string&)>;

    FileLog(std::filesystem::path directory, Warn warn);

    bool active() const noexcept { return active_; }
    const std::filesystem::path& directory() const noexcept { return directory_; }

    void gameStarted(const GameSetupInfo& setup) override;
    void dayStarted(int day, const std::vector<int>& alive) override;
    void statement(int day, int speaker, const Response& response, const std::string& text) override;
    void voteCast(int day, int voter, const Response& response) override;
    void votingFinished(int day, const std::vector<VoteRow>& rows, int executed, bool coinToss) override;
    void nightStarted(int night) override;
    void nightAction(int night, int actor, const Response& response) override;
    void nightResolved(int night, const NightOutcome& outcome) override;
    void playerEliminated(int day, int player, const std::vector<DeathCause>& causes) override;
    void gameEnded(Winner winner, int days) override;

private:
    struct PlayerStats {
        int statements = 0;
        int accusationsMade = 0;
        int timesAccused = 0;
        int votesCast = 0;
        int votesAgainstMafia = 0;  // голоса против настоящей мафии (включая ниндзя)
        int votesReceived = 0;
        int nightActions = 0;
        int timesSaved = 0;  // спасён доктором или телохранителем
        int deathDay = 0;    // 0 — дожил до конца
        std::vector<DeathCause> deathCauses;
    };

    void openRound(int round);
    void closeRound();
    void write(const std::string& line);
    void disable(const std::string& reason);
    void writeSummary(Winner winner, int days);

    std::string name(int player) const;
    PlayerStats& stats(int player);
    Role roleOf(int player) const;

    std::filesystem::path directory_;
    Warn warn_;
    bool active_ = true;
    GameSetupInfo setup_;
    std::vector<PlayerStats> stats_;
    std::ofstream round_;
    std::filesystem::path roundPath_;
    int voteHeaderDay_ = 0;  // день, для которого уже написан заголовок голосования
    int executions_ = 0;
    int nightDeaths_ = 0;
    int saves_ = 0;
};

}  // namespace mafia
