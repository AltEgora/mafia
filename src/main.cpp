#include <iostream>
#include <random>

#include "mafia/config.hpp"
#include "mafia/moderator.hpp"
#include "mafia/roles_config.hpp"

int main(int argc, char* argv[]) {
    const mafia::ParseResult parsed = mafia::parseArgs(argc, argv);
    if (parsed.help) {
        std::cout << mafia::usage(argv[0]);
        return 0;
    }
    if (!parsed.config) {
        std::cerr << "Ошибка: " << parsed.error << "\n\n" << mafia::usage(argv[0]);
        return 1;
    }

    mafia::GameConfig config = *parsed.config;
    mafia::RoleConfig roles = mafia::defaultRoleConfig();
    if (!config.rolesConfig.empty()) {
        try {
            roles = mafia::loadRoleConfig(config.rolesConfig);
            mafia::buildRoles(roles, config.players, config.mafiaDivisor);  // набор подходит для N игроков?
        } catch (const mafia::ConfigError& error) {
            std::cerr << "Ошибка в наборе ролей: " << error.what() << '\n';
            return 1;
        }
    }
    if (!config.seedGiven) {
        std::random_device device;
        config.seed = (static_cast<std::uint64_t>(device()) << 32) ^ device();
    }
    // Ввод читает нить игрока-человека; приглашения ведущий сбрасывает явно,
    // поэтому связь cin с cout не нужна.
    std::cin.tie(nullptr);

    mafia::Moderator moderator(config, std::move(roles), std::cin, std::cout);
    return moderator.run() == mafia::Winner::Aborted ? 2 : 0;
}
