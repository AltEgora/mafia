#pragma once

// Набор ролей партии. Задаётся YAML-файлом (--roles-config), например:
//
//   roles:
//     mafia: auto       # auto — floor(N/k), но не меньше 1 (k задаёт --mafia-k); или число
//     ninja: 1          # ниндзя входит в число мафии
//     commissar: 1
//     doctor: 1
//     maniac: 1
//     journalist: 1
//     bodyguard: 1
//
// Роли, которых нет в файле, в партии не участвуют; мирные жители заполняют
// оставшиеся места. Порядок особых ролей в файле — их приоритет, если мест
// на всех не хватает.

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "mafia/roles.hpp"

namespace mafia {

// Ошибка в файле ролей; what() уже содержит «файл:строка: описание».
class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Раздел ai_agents: ИИ-агенты вместо части ботов.
struct AiAgentsConfig {
    std::optional<int> count;                  // флаг --ai-agents важнее
    std::string model;                         // пусто — модель по умолчанию
    std::vector<std::string> characteristics;  // пусто — у всех нейтральная характеристика
};

struct RoleConfig {
    struct Special {
        Role role;
        int line = 0;  // строка в файле (0 — встроенный набор)
    };

    std::optional<int> mafia;       // nullopt — auto
    bool ninja = false;
    std::vector<Special> specials;  // комиссар, доктор, маньяк, журналист, телохранитель — по приоритету
    std::string source;             // имя файла; пусто для встроенного набора
    AiAgentsConfig ai;
};

// Классический набор: мафия auto, комиссар, доктор, маньяк.
RoleConfig defaultRoleConfig();

RoleConfig parseRoleConfig(std::string_view text, const std::string& source);
RoleConfig loadRoleConfig(const std::filesystem::path& path);

struct RoleSetup {
    std::vector<Role> roles;            // до перемешивания
    std::vector<std::string> skipped;   // сообщения о ролях, которым не хватило места
};

// Раскладывает роли на N мест; mafia: auto даёт floor(N / mafiaDivisor), но не меньше 1.
// Один мирный житель остаётся всегда. Бросает ConfigError, если мафии слишком много.
RoleSetup buildRoles(const RoleConfig& config, int players, int mafiaDivisor = kDefaultMafiaDivisor);

}  // namespace mafia
