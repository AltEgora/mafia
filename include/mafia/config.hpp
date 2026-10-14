#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mafia {

struct GameConfig {
    int players = 0;
    bool interactive = false;
    bool openAnnouncements = false;
    bool fullLog = false;
    bool pauses = true;  // паузы между репликами и фазами
    int mafiaDivisor = 3;  // k в формуле числа мафии max(1, floor(N / k)), k >= 3
    bool coroutines = false;  // игроки — корутины вместо отдельных нитей
    int aiAgents = -1;        // --ai-agents K: сколько ботов заменить ИИ-агентами; -1 — не задано
    std::uint64_t seed = 0;
    bool seedGiven = false;
    std::string rolesConfig;  // YAML-файл с набором ролей; пусто — классический набор
    std::string logDir;       // каталог для файловых логов; пусто — без файлов
};

struct ParseResult {
    std::optional<GameConfig> config;
    bool help = false;
    std::string error;
};

inline constexpr int kMinPlayers = 5;
inline constexpr int kMaxPlayers = 1500;

ParseResult parseArgs(int argc, const char* const argv[]);
std::string usage(std::string_view program);

}  // namespace mafia
