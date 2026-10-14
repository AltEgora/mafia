#include "mafia/roles.hpp"

#include <algorithm>

namespace mafia {

std::string_view roleName(Role role) {
    switch (role) {
        case Role::Civilian:
            return "Мирный житель";
        case Role::Mafia:
            return "Мафия";
        case Role::Commissar:
            return "Комиссар";
        case Role::Doctor:
            return "Доктор";
        case Role::Maniac:
            return "Маньяк";
        case Role::Journalist:
            return "Журналист";
        case Role::Bodyguard:
            return "Телохранитель";
        case Role::Ninja:
            return "Ниндзя";
    }
    return "?";
}

std::string_view roleNameLower(Role role) {
    switch (role) {
        case Role::Civilian:
            return "мирный житель";
        case Role::Mafia:
            return "мафия";
        case Role::Commissar:
            return "комиссар";
        case Role::Doctor:
            return "доктор";
        case Role::Maniac:
            return "маньяк";
        case Role::Journalist:
            return "журналист";
        case Role::Bodyguard:
            return "телохранитель";
        case Role::Ninja:
            return "ниндзя";
    }
    return "?";
}

std::string_view sideName(Side side) {
    return side == Side::Mafia ? "мафия" : "мирный";
}

std::string_view teamName(Team team) {
    switch (team) {
        case Team::Town:
            return "мирные";
        case Team::Mafia:
            return "мафия";
        case Team::Maniac:
            return "маньяк";
    }
    return "?";
}

Team teamOf(Role role) {
    switch (role) {
        case Role::Mafia:
        case Role::Ninja:
            return Team::Mafia;
        case Role::Maniac:
            return Team::Maniac;
        default:
            return Team::Town;
    }
}

Side checkSide(Role role) {
    return role == Role::Mafia ? Side::Mafia : Side::Town;
}

Side announcedSide(Role role) {
    return teamOf(role) == Team::Mafia ? Side::Mafia : Side::Town;
}

int mafiaCount(int players, int divisor) {
    return std::max(1, players / divisor);
}

}  // namespace mafia
