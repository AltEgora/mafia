#include "mafia/config.hpp"

#include <charconv>

namespace mafia {

namespace {

template <class Number>
bool parseNumber(std::string_view text, Number& value) {
    const auto [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && ptr == text.data() + text.size();
}

}  // namespace

ParseResult parseArgs(int argc, const char* const argv[]) {
    ParseResult result;
    GameConfig config;
    bool playersGiven = false;

    auto fail = [&result](std::string message) {
        result.error = std::move(message);
        return result;
    };

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        std::optional<std::string_view> inlineValue;
        if (arg.starts_with("--")) {
            if (const auto eq = arg.find('='); eq != std::string_view::npos) {
                inlineValue = arg.substr(eq + 1);
                arg = arg.substr(0, eq);
            }
        }
        auto value = [&]() -> std::optional<std::string_view> {
            if (inlineValue) {
                return inlineValue;
            }
            if (i + 1 < argc) {
                return std::string_view(argv[++i]);
            }
            return std::nullopt;
        };

        if (arg == "-h" || arg == "--help") {
            result.help = true;
            return result;
        } else if (arg == "--players" || arg == "-n") {
            const auto text = value();
            if (!text || !parseNumber(*text, config.players)) {
                return fail("после --players нужно целое число игроков");
            }
            playersGiven = true;
        } else if (arg == "--interactive") {
            config.interactive = true;
        } else if (arg == "--open-announcements") {
            config.openAnnouncements = true;
        } else if (arg == "--closed-announcements") {
            config.openAnnouncements = false;
        } else if (arg == "--full-log") {
            config.fullLog = true;
        } else if (arg == "--ai-agents") {
            const auto text = value();
            if (!text || !parseNumber(*text, config.aiAgents) || config.aiAgents < 0) {
                return fail("после --ai-agents нужно целое число не меньше 0");
            }
        } else if (arg == "--coroutines") {
            config.coroutines = true;
        } else if (arg == "--no-pauses") {
            config.pauses = false;
        } else if (arg == "--mafia-k") {
            const auto text = value();
            if (!text || !parseNumber(*text, config.mafiaDivisor) || config.mafiaDivisor < 3) {
                return fail("после --mafia-k нужно целое число не меньше 3");
            }
        } else if (arg == "--roles-config") {
            const auto text = value();
            if (!text || text->empty()) {
                return fail("после --roles-config нужен путь к YAML-файлу");
            }
            config.rolesConfig = std::string(*text);
        } else if (arg == "--log-dir") {
            const auto text = value();
            if (!text || text->empty()) {
                return fail("после --log-dir нужен путь к каталогу");
            }
            config.logDir = std::string(*text);
        } else if (arg == "--seed") {
            const auto text = value();
            if (!text || !parseNumber(*text, config.seed)) {
                return fail("после --seed нужно неотрицательное целое число");
            }
            config.seedGiven = true;
        } else {
            return fail("неизвестный параметр «" + std::string(arg) + "»");
        }
    }

    if (!playersGiven) {
        return fail("укажите число игроков: --players N");
    }
    if (config.players < kMinPlayers || config.players > kMaxPlayers) {
        return fail("число игроков должно быть от " + std::to_string(kMinPlayers) + " до " +
                    std::to_string(kMaxPlayers) + " (N > 4)");
    }
    result.config = config;
    return result;
}

std::string usage(std::string_view program) {
    std::string text;
    text += "Использование: ";
    text += program;
    text += " --players N [--interactive] [--open-announcements | --closed-announcements]\n"
            "       [--full-log] [--no-pauses] [--seed S] [--mafia-k K] [--roles-config FILE]\n"
            "       [--log-dir DIR] [--coroutines] [--ai-agents K]\n\n"
            "  --players N, -n N       число игроков, N > 4\n"
            "  --interactive           вы играете вместе с ботами (иначе играют только боты)\n"
            "  --open-announcements    ведущий раскрывает роли погибших и обстоятельства ночи\n"
            "  --closed-announcements  ведущий называет только сторону погибшего (по умолчанию)\n"
            "  --full-log              полный лог: роли, ночные ходы, рассуждения ботов\n"
            "  --no-pauses             без пауз между репликами и фазами (для тестов)\n"
            "  --coroutines            игроки — корутины C++20 в нити ведущего (по умолчанию — нити)\n"
            "  --ai-agents K           K ботов заменить ИИ-агентами (бесплатный OpenRouter, ключ в\n"
            "                          OPENROUTER_API_KEY; без ключа агенты играют как обычные боты)\n"
            "  --seed S                сид генератора — партия повторяется один в один\n"
            "  --mafia-k K             мафии max(1, floor(N / K)); K >= 3, по умолчанию 3\n"
            "  --roles-config FILE     набор ролей из YAML-файла (см. configs/)\n"
            "  --log-dir DIR           писать логи раундов и итоговую статистику в DIR/game_<сид>/\n"
            "  --help, -h              эта справка\n\n"
            "Пример: ";
    text += program;
    text += " --players 10 --interactive --open-announcements --full-log\n";
    return text;
}

}  // namespace mafia
