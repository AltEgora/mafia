// Тесты конфигурации: файл ролей (YAML), раскладка ролей на N мест и разбор аргументов.

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "mafia/config.hpp"
#include "mafia/roles_config.hpp"
#include "test_framework.hpp"

using mafia::buildRoles;
using mafia::ConfigError;
using mafia::parseRoleConfig;
using mafia::Role;

namespace {

const std::filesystem::path kConfigs = std::filesystem::path(MAFIA_SOURCE_DIR) / "configs";

long countRole(const std::vector<Role>& roles, Role role) {
    return std::ranges::count(roles, role);
}

mafia::ParseResult parse(std::vector<const char*> args) {
    args.insert(args.begin(), "mafia");
    return mafia::parseArgs(static_cast<int>(args.size()), args.data());
}

}  // namespace

TEST(DefaultLayoutIsClassic) {
    const auto setup = buildRoles(mafia::defaultRoleConfig(), 7);
    const std::vector<Role> expected = {Role::Mafia,  Role::Mafia,    Role::Commissar, Role::Doctor,
                                        Role::Maniac, Role::Civilian, Role::Civilian};
    CHECK(setup.roles == expected);
    CHECK(setup.skipped.empty());
}

// Без --roles-config и с configs/classic.yaml раскладка одинакова для любого N.
TEST(ClassicFileMatchesDefault) {
    const auto classic = mafia::loadRoleConfig(kConfigs / "classic.yaml");
    for (int players = 5; players <= 60; ++players) {
        CHECK(buildRoles(classic, players).roles == buildRoles(mafia::defaultRoleConfig(), players).roles);
    }
}

TEST(ExtendedFileHasAllRoles) {
    const auto extended = mafia::loadRoleConfig(kConfigs / "extended.yaml");
    const auto setup = buildRoles(extended, 10);
    const std::vector<Role> expected = {Role::Mafia,      Role::Mafia,     Role::Ninja,    Role::Commissar,
                                        Role::Doctor,     Role::Maniac,    Role::Journalist, Role::Bodyguard,
                                        Role::Civilian,   Role::Civilian};
    CHECK(setup.roles == expected);
    CHECK(setup.skipped.empty());
    CHECK_EQ(extended.source, (kConfigs / "extended.yaml").string());
}

// При пяти игроках места хватает только на мафию, трёх особых и одного мирного.
TEST(SkipsRolesThatDoNotFit) {
    const auto extended = mafia::loadRoleConfig(kConfigs / "extended.yaml");
    const auto setup = buildRoles(extended, 5);
    CHECK_EQ(setup.roles.size(), std::size_t{5});
    CHECK_EQ(countRole(setup.roles, Role::Mafia), 1L);
    CHECK_EQ(countRole(setup.roles, Role::Ninja), 0L);
    CHECK_EQ(countRole(setup.roles, Role::Civilian), 1L);
    CHECK_EQ(countRole(setup.roles, Role::Journalist), 0L);
    CHECK_EQ(setup.skipped.size(), std::size_t{3});
    CHECK(setup.skipped[0].find("Ниндзя") != std::string::npos);
    CHECK(setup.skipped[1].find("«Журналист» пропущена: при 5 игроках") != std::string::npos);
    CHECK(setup.skipped[2].find("Телохранитель") != std::string::npos);
}

TEST(FileOrderIsPriority) {
    const auto config = parseRoleConfig("roles:\n  bodyguard: 1\n  journalist: yes\n  commissar: 1\n  doctor: 1\n", "t");
    const auto setup = buildRoles(config, 5);
    const std::vector<Role> expected = {Role::Mafia, Role::Bodyguard, Role::Journalist, Role::Commissar,
                                        Role::Civilian};
    CHECK(setup.roles == expected);
    CHECK_EQ(setup.skipped.size(), std::size_t{1});
}

TEST(ExplicitMafiaAndDisabledRoles) {
    const auto config = parseRoleConfig("roles:\n  mafia: 2\n  doctor: 0\n  maniac: false\n  commissar: 1\n", "t");
    CHECK(config.mafia == 2);
    const auto setup = buildRoles(config, 5);
    CHECK_EQ(countRole(setup.roles, Role::Mafia), 2L);
    CHECK_EQ(countRole(setup.roles, Role::Doctor), 0L);
    CHECK_EQ(countRole(setup.roles, Role::Maniac), 0L);
    CHECK_EQ(countRole(setup.roles, Role::Civilian), 2L);

    CHECK_THROWS_WITH(buildRoles(parseRoleConfig("roles:\n  mafia: 3\n", "big.yaml"), 6), ConfigError,
                      "big.yaml: мафии (3) должно быть меньше половины игроков (6)");
}

TEST(MafiaDivisorChangesAutoCount) {
    const auto config = mafia::defaultRoleConfig();
    CHECK_EQ(countRole(buildRoles(config, 12).roles, Role::Mafia), 4L);     // k = 3 по умолчанию
    CHECK_EQ(countRole(buildRoles(config, 12, 4).roles, Role::Mafia), 3L);
    CHECK_EQ(countRole(buildRoles(config, 12, 5).roles, Role::Mafia), 2L);
    CHECK_EQ(countRole(buildRoles(config, 6, 10).roles, Role::Mafia), 1L);  // не меньше одного
    // Ниндзя занимает место в мафии, посчитанной с тем же k.
    const auto extended = mafia::loadRoleConfig(kConfigs / "extended.yaml");
    const auto setup = buildRoles(extended, 12, 4);
    CHECK_EQ(countRole(setup.roles, Role::Mafia) + countRole(setup.roles, Role::Ninja), 3L);
    // Явное число мафии в файле важнее k.
    CHECK_EQ(countRole(buildRoles(parseRoleConfig("roles:\n  mafia: 2\n", "t"), 12, 6).roles, Role::Mafia), 2L);
}

TEST(AiAgentsSection) {
    const auto config = parseRoleConfig(
        "roles:\n  doctor: 1\nai_agents:\n  count: 2\n  model: meta-llama/llama-3.3-70b-instruct:free\n  characteristics:\n"
        "    - плохо врёт\n    - логик\n",
        "a.yaml");
    CHECK(config.ai.count == 2);
    CHECK_EQ(config.ai.model, std::string("meta-llama/llama-3.3-70b-instruct:free"));
    CHECK(config.ai.characteristics == (std::vector<std::string>{"плохо врёт", "логик"}));
    CHECK_EQ(config.specials.size(), std::size_t{1});

    // Только агенты — роли классические.
    const auto onlyAgents = parseRoleConfig("ai_agents:\n  count: 1\n  characteristics: всегда за игрока 1\n", "b.yaml");
    CHECK(onlyAgents.ai.count == 1);
    CHECK(onlyAgents.ai.characteristics == std::vector<std::string>{"всегда за игрока 1"});
    CHECK(buildRoles(onlyAgents, 7).roles == buildRoles(mafia::defaultRoleConfig(), 7).roles);
    CHECK(!mafia::defaultRoleConfig().ai.count);

    const auto extended = mafia::loadRoleConfig(kConfigs / "ai_agents.yaml");
    CHECK(extended.ai.count == 3);
    CHECK_EQ(extended.ai.characteristics.size(), std::size_t{10});

    CHECK_THROWS_WITH(parseRoleConfig("ai_agents:\n  count: -1\n", "e.yaml"), ConfigError, "e.yaml:2: «ai_agents.count»");
    CHECK_THROWS_WITH(parseRoleConfig("ai_agents:\n  count: два\n", "e.yaml"), ConfigError, "не меньше 0");
    CHECK_THROWS_WITH(parseRoleConfig("ai_agents:\n  temperature: 1\n", "e.yaml"), ConfigError,
                      "e.yaml:2: неизвестный ключ «temperature» в ai_agents");
    CHECK_THROWS_WITH(parseRoleConfig("ai_agents: 3\n", "e.yaml"), ConfigError, "должен быть словарём");
    CHECK_THROWS_WITH(parseRoleConfig("ai_agents:\n  characteristics:\n    - [a, b]\n", "e.yaml"), ConfigError,
                      "e.yaml:3: характеристика агента");
    CHECK_THROWS_WITH(parseRoleConfig("ai_agents:\n  model:\n", "e.yaml"), ConfigError, "e.yaml:2: «ai_agents.model»");
}

TEST(ConfigErrorsHaveFileAndLine) {
    CHECK_THROWS_WITH(parseRoleConfig("roles:\n  wizard: 1\n", "r.yaml"), ConfigError,
                      "r.yaml:2: неизвестная роль «wizard»");
    CHECK_THROWS_WITH(parseRoleConfig("roles:\n  doctor: 2\n", "r.yaml"), ConfigError, "r.yaml:2: роль «doctor»");
    CHECK_THROWS_WITH(parseRoleConfig("roles:\n  doctor:\n", "r.yaml"), ConfigError, "r.yaml:2:");
    CHECK_THROWS_WITH(parseRoleConfig("roles:\n  mafia: many\n", "r.yaml"), ConfigError, "r.yaml:2: «mafia»");
    CHECK_THROWS_WITH(parseRoleConfig("roles:\n  mafia: 0\n", "r.yaml"), ConfigError, "не меньше 1");
    CHECK_THROWS_WITH(parseRoleConfig("roles:\n  civilian: 3\n", "r.yaml"), ConfigError, "автоматически");
    CHECK_THROWS_WITH(parseRoleConfig("roles:\n  - doctor\n", "r.yaml"), ConfigError, "r.yaml:2: «roles» должен быть словарём");
    CHECK_THROWS_WITH(parseRoleConfig("players: 5\n", "r.yaml"), ConfigError, "r.yaml:1: неизвестный ключ «players»");
    CHECK_THROWS_WITH(parseRoleConfig("", "r.yaml"), ConfigError, "нет раздела «roles»");
    CHECK_THROWS_WITH(parseRoleConfig("- 1\n", "r.yaml"), ConfigError, "ожидался словарь");
    // Ошибка синтаксиса YAML приходит с именем файла и номером строки.
    CHECK_THROWS_WITH(parseRoleConfig("roles:\n  doctor: 1\n  doctor: 1\n", "r.yaml"), ConfigError,
                      "r.yaml:3: повторяющийся ключ «doctor»");
    CHECK_THROWS_WITH(mafia::loadRoleConfig(kConfigs / "no-such-file.yaml"), ConfigError, "файл не найден");
}

TEST(CommandLineOptions) {
    const auto full = parse({"--players", "9", "--roles-config", "configs/extended.yaml", "--log-dir=logs",
                             "--no-pauses", "--seed", "5"});
    CHECK(full.config.has_value());
    CHECK_EQ(full.config->players, 9);
    CHECK_EQ(full.config->rolesConfig, std::string("configs/extended.yaml"));
    CHECK_EQ(full.config->logDir, std::string("logs"));
    CHECK(!full.config->pauses);
    CHECK_EQ(full.config->seed, std::uint64_t{5});

    const auto defaults = parse({"-n", "6"});
    CHECK(defaults.config && defaults.config->rolesConfig.empty() && defaults.config->logDir.empty());
    CHECK(defaults.config->pauses && !defaults.config->openAnnouncements);

    CHECK(parse({"--players", "4"}).error.find("N > 4") != std::string::npos);
    CHECK(parse({"--players", "7", "--roles-config"}).error.find("--roles-config") != std::string::npos);
    CHECK(parse({"--players", "7", "--log-dir="}).error.find("--log-dir") != std::string::npos);
    CHECK(parse({"--players", "7", "--wat"}).error.find("неизвестный параметр") != std::string::npos);
    CHECK(parse({"--help"}).help);

    CHECK_EQ(defaults.config->mafiaDivisor, 3);
    CHECK(!defaults.config->coroutines);
    const auto tuned = parse({"-n", "9", "--mafia-k", "4", "--coroutines"});
    CHECK(tuned.config && tuned.config->mafiaDivisor == 4 && tuned.config->coroutines);
    CHECK(parse({"-n", "9", "--mafia-k=5"}).config->mafiaDivisor == 5);
    CHECK(parse({"-n", "9", "--mafia-k", "2"}).error.find("не меньше 3") != std::string::npos);
    CHECK(parse({"-n", "9", "--mafia-k", "три"}).error.find("--mafia-k") != std::string::npos);
    CHECK(parse({"-n", "9", "--mafia-k"}).error.find("--mafia-k") != std::string::npos);

    CHECK_EQ(defaults.config->aiAgents, -1);  // флаг не задан — решает YAML
    CHECK_EQ(parse({"-n", "9", "--ai-agents", "3"}).config->aiAgents, 3);
    CHECK_EQ(parse({"-n", "9", "--ai-agents=0"}).config->aiAgents, 0);
    CHECK(parse({"-n", "9", "--ai-agents", "-2"}).error.find("--ai-agents") != std::string::npos);
}

MAFIA_TEST_MAIN()
