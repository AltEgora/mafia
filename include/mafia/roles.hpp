#pragma once

#include <string_view>

namespace mafia {

enum class Role { Civilian, Mafia, Commissar, Doctor, Maniac, Journalist, Bodyguard, Ninja };

inline constexpr Role kAllRoles[] = {Role::Civilian, Role::Mafia,      Role::Commissar, Role::Doctor,
                                     Role::Maniac,   Role::Journalist, Role::Bodyguard, Role::Ninja};

// Реальный лагерь игрока: за кого он играет и с кем побеждает.
enum class Team { Town, Mafia, Maniac };

// Сторона в объявлениях и проверках: «мирный» или «мафия».
enum class Side { Town, Mafia };

std::string_view roleName(Role role);       // «Мирный житель», «Ниндзя», ...
std::string_view roleNameLower(Role role);  // «мирный житель», «ниндзя», ... — для перечислений
std::string_view sideName(Side side);
std::string_view teamName(Team team);       // «мирные», «мафия», «маньяк»

Team teamOf(Role role);

// Как игрока видят комиссар и журналист: маньяк и ниндзя выглядят мирными.
Side checkSide(Role role);

// Что ведущий называет при закрытых объявлениях: ниндзя — мафия, маньяк — мирный.
Side announcedSide(Role role);

// Ходит ли роль ночью. Каждой такой роли должен соответствовать класс,
// удовлетворяющий концепту NightRole (проверяется в role_dispatch.hpp).
constexpr bool hasNightAction(Role role) {
    return role != Role::Civilian;
}

// Число мафиози (вместе с ниндзя): floor(N / k), но не меньше одного; k >= 3.
inline constexpr int kDefaultMafiaDivisor = 3;
int mafiaCount(int players, int divisor = kDefaultMafiaDivisor);

}  // namespace mafia
