#include "mafia/roles_config.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <sstream>
#include <utility>

#include "mafia/yaml.hpp"

namespace mafia {

namespace {

struct SpecialKey {
    std::string_view key;
    Role role;
};

constexpr std::array<SpecialKey, 5> kSpecialKeys = {{
    {"commissar", Role::Commissar},
    {"doctor", Role::Doctor},
    {"maniac", Role::Maniac},
    {"journalist", Role::Journalist},
    {"bodyguard", Role::Bodyguard},
}};

constexpr std::string_view kAllowedKeys = "mafia, ninja, commissar, doctor, maniac, journalist, bodyguard";

[[noreturn]] void fail(const std::string& source, int line, const std::string& message) {
    throw ConfigError(source + ":" + std::to_string(line) + ": " + message);
}

// Особая роль присутствует (1) или нет (0); true/false/yes/no тоже допустимы.
bool parseFlag(const yaml::Entry& entry, const std::string& source) {
    const yaml::Node& value = entry.value;
    if (!value.isScalar() || value.isNull()) {
        fail(source, entry.line, "у роли «" + entry.key + "» должно быть значение 0 или 1");
    }
    const std::string& text = value.scalar;
    if (text == "1" || text == "true" || text == "yes") {
        return true;
    }
    if (text == "0" || text == "false" || text == "no") {
        return false;
    }
    fail(source, entry.line,
         "роль «" + entry.key + "»: допустимо 0 или 1 (особая роль — не больше одного игрока), получено «" + text +
             "»");
}

std::optional<int> parseMafia(const yaml::Entry& entry, const std::string& source) {
    const yaml::Node& value = entry.value;
    if (value.isScalar() && value.scalar == "auto") {
        return std::nullopt;
    }
    int count = 0;
    if (value.isScalar() && !value.isNull()) {
        const std::string& text = value.scalar;
        const auto [ptr, error] = std::from_chars(text.data(), text.data() + text.size(), count);
        if (error == std::errc{} && ptr == text.data() + text.size() && count >= 1) {
            return count;
        }
    }
    fail(source, entry.line, "«mafia»: ожидалось auto или целое число не меньше 1");
}

// ai_agents:
//   count: 2                     # сколько ботов заменить ИИ-агентами
//   model: openrouter/free       # необязательно; по умолчанию — kDefaultLlmModel
//   characteristics:             # необязательно; агентам достаются случайные, разные;
//                                # без списка — нейтральная из кода
//     - плохо врёт
AiAgentsConfig parseAiAgents(const yaml::Node& node, const std::string& source) {
    if (!node.isMapping()) {
        fail(source, node.line, "«ai_agents» должен быть словарём: count, model, characteristics");
    }
    AiAgentsConfig ai;
    for (const yaml::Entry& entry : node.entries) {
        const yaml::Node& value = entry.value;
        if (entry.key == "count") {
            int count = -1;
            if (value.isScalar() && !value.isNull()) {
                const auto [ptr, error] =
                    std::from_chars(value.scalar.data(), value.scalar.data() + value.scalar.size(), count);
                if (error != std::errc{} || ptr != value.scalar.data() + value.scalar.size()) {
                    count = -1;
                }
            }
            if (count < 0) {
                fail(source, entry.line, "«ai_agents.count»: ожидалось целое число не меньше 0");
            }
            ai.count = count;
        } else if (entry.key == "model") {
            if (!value.isScalar() || value.scalar.empty()) {
                fail(source, entry.line, "«ai_agents.model»: ожидалось имя модели");
            }
            ai.model = value.scalar;
        } else if (entry.key == "characteristics") {
            const auto addCharacteristic = [&](const yaml::Node& item) {
                if (!item.isScalar() || item.scalar.empty()) {
                    fail(source, item.line, "характеристика агента должна быть непустой строкой");
                }
                ai.characteristics.push_back(item.scalar);
            };
            if (value.isSequence()) {
                std::ranges::for_each(value.items, addCharacteristic);
            } else {
                addCharacteristic(value);
            }
        } else {
            fail(source, entry.line,
                 "неизвестный ключ «" + entry.key + "» в ai_agents; допустимы: count, model, characteristics");
        }
    }
    return ai;
}

}  // namespace

RoleConfig defaultRoleConfig() {
    RoleConfig config;
    config.specials = {{Role::Commissar, 0}, {Role::Doctor, 0}, {Role::Maniac, 0}};
    return config;
}

RoleConfig parseRoleConfig(std::string_view text, const std::string& source) {
    yaml::Node root;
    try {
        root = yaml::parse(text);
    } catch (const yaml::ParseError& error) {
        fail(source, error.line(), error.detail());
    }

    if (!root.isMapping()) {
        fail(source, root.line, "ожидался словарь с разделом «roles», а не " + std::string(yaml::kindName(root.kind)));
    }
    for (const yaml::Entry& entry : root.entries) {
        if (entry.key != "roles" && entry.key != "ai_agents") {
            fail(source, entry.line,
                 "неизвестный ключ «" + entry.key + "» — в файле ожидаются разделы «roles» и «ai_agents»");
        }
    }
    const yaml::Node* roles = root.find("roles");
    const yaml::Node* agents = root.find("ai_agents");
    if (roles == nullptr && agents == nullptr) {
        fail(source, root.line, "нет раздела «roles» (или «ai_agents»)");
    }
    if (roles == nullptr) {
        // Только настройки агентов — роли классические.
        RoleConfig config = defaultRoleConfig();
        config.source = source;
        config.ai = parseAiAgents(*agents, source);
        return config;
    }
    if (!roles->isMapping()) {
        fail(source, roles->line,
             "«roles» должен быть словарём «роль: количество», а не " + std::string(yaml::kindName(roles->kind)));
    }

    RoleConfig config;
    config.source = source;
    if (agents != nullptr) {
        config.ai = parseAiAgents(*agents, source);
    }
    for (const yaml::Entry& entry : roles->entries) {
        if (entry.key == "mafia") {
            config.mafia = parseMafia(entry, source);
            continue;
        }
        if (entry.key == "ninja") {
            config.ninja = parseFlag(entry, source);
            continue;
        }
        if (entry.key == "civilian" || entry.key == "civilians") {
            fail(source, entry.line, "мирные жители заполняют оставшиеся места автоматически — ключ «" + entry.key +
                                         "» не нужен");
        }
        bool known = false;
        for (const SpecialKey& special : kSpecialKeys) {
            if (entry.key == special.key) {
                known = true;
                if (parseFlag(entry, source)) {
                    config.specials.push_back({special.role, entry.line});
                }
            }
        }
        if (!known) {
            fail(source, entry.line,
                 "неизвестная роль «" + entry.key + "»; допустимы: " + std::string(kAllowedKeys));
        }
    }
    return config;
}

RoleConfig loadRoleConfig(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        throw ConfigError(path.string() + ": файл не найден");
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ConfigError(path.string() + ": не удалось открыть файл");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parseRoleConfig(buffer.str(), path.string());
}

RoleSetup buildRoles(const RoleConfig& config, int players, int mafiaDivisor) {
    const int mafiaTotal = config.mafia.value_or(mafiaCount(players, mafiaDivisor));
    if (mafiaTotal * 2 >= players) {
        throw ConfigError((config.source.empty() ? "" : config.source + ": ") + "мафии (" +
                          std::to_string(mafiaTotal) + ") должно быть меньше половины игроков (" +
                          std::to_string(players) + ")");
    }

    RoleSetup setup;
    int regularMafia = mafiaTotal;
    bool ninja = false;
    if (config.ninja) {
        if (mafiaTotal >= 2) {
            --regularMafia;
            ninja = true;
        } else {
            setup.skipped.push_back("Роль «Ниндзя» пропущена: при " + std::to_string(players) +
                                    " игроках мафиози один, и он остаётся обычным.");
        }
    }
    setup.roles.assign(static_cast<std::size_t>(regularMafia), Role::Mafia);
    if (ninja) {
        setup.roles.push_back(Role::Ninja);
    }

    int freeSlots = players - mafiaTotal - 1;  // одно место всегда за мирным жителем
    for (const RoleConfig::Special& special : config.specials) {
        if (freeSlots > 0) {
            setup.roles.push_back(special.role);
            --freeSlots;
        } else {
            setup.skipped.push_back("Роль «" + std::string(roleName(special.role)) + "» пропущена: при " +
                                    std::to_string(players) + " игроках для неё не хватило места.");
        }
    }
    setup.roles.resize(static_cast<std::size_t>(players), Role::Civilian);
    return setup;
}

}  // namespace mafia
