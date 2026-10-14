#pragma once

#include <utility>

// Управление ночными ходами у ведущего. Ведущий знает роль игрока только как
// значение Role; visitNightRole находит по нему класс роли и передаёт его в
// шаблонный обработчик. Попасть в список ночных ролей и в обработчик может
// только класс, удовлетворяющий концепту NightRole: мирный житель там не
// скомпилируется.

#include "mafia/bots.hpp"
#include "mafia/player_concepts.hpp"
#include "mafia/roles.hpp"

namespace mafia {

template <class... Roles>
struct RoleList {};

// Все классы ролей с ночным ходом.
using NightRoles = RoleList<Mafioso, Ninja, Commissar, Doctor, Maniac, Journalist, Bodyguard>;

// Все классы ролей ботов — по ним ведущий создаёт игроков (ботов и ИИ-агентов).
using AllRoles = RoleList<Civilian, Mafioso, Ninja, Commissar, Doctor, Maniac, Journalist, Bodyguard>;

// Вызывает visitor.template operator()<Bot>() для класса роли с Bot::kRole == role.
template <RoleBot... Bots, class Visitor>
bool visitRoleBot(RoleList<Bots...>, Role role, Visitor&& visitor) {
    return ((role == Bots::kRole ? (visitor.template operator()<Bots>(), true) : false) || ...);
}

template <class Visitor>
bool visitRoleBot(Role role, Visitor&& visitor) {
    return visitRoleBot(AllRoles{}, role, std::forward<Visitor>(visitor));
}

// У каждой роли ровно один класс в AllRoles.
template <RoleBot... Bots>
constexpr bool coversAllRoles(RoleList<Bots...>) {
    for (Role role : kAllRoles) {
        if (((Bots::kRole == role ? 1 : 0) + ...) != 1) {
            return false;
        }
    }
    return true;
}

static_assert(coversAllRoles(AllRoles{}), "у каждой роли должен быть ровно один класс в AllRoles");

// Вызывает visitor.template operator()<R>() для класса R из списка, у которого
// R::kRole == role. Возвращает false, если у роли нет ночного хода.
template <NightRole... Roles, class Visitor>
bool visitNightRole(RoleList<Roles...>, Role role, Visitor&& visitor) {
    return ((role == Roles::kRole ? (visitor.template operator()<Roles>(), true) : false) || ...);
}

template <class Visitor>
bool visitNightRole(Role role, Visitor&& visitor) {
    return visitNightRole(NightRoles{}, role, std::forward<Visitor>(visitor));
}

// Добавляет проверенный ответ игрока с ролью R в ходы ночи. Доступно только
// ночным ролям: для мирного жителя этот вызов не скомпилируется.
template <NightRole R>
void collectNightMove(NightMoves& moves, int actor, const Response& response) {
    R::recordNightMove(moves, actor, response);
}

// Собирает все ночные ходы: роль каждого ответившего определяет класс R.
// answers — ответы по возрастанию номеров (первым из мафии идёт босс).
template <class Answers>
NightMoves collectNightMoves(const Answers& answers, auto roleOf) {
    NightMoves moves;
    for (const auto& [actor, response] : answers) {
        visitNightRole(roleOf(actor), [&]<NightRole R>() { collectNightMove<R>(moves, actor, response); });
    }
    return moves;
}

// Список ночных ролей совпадает с ролями, которые ходят ночью (hasNightAction).
template <NightRole... Roles>
constexpr bool coversNightRoles(RoleList<Roles...>) {
    for (Role role : kAllRoles) {
        const int listed = ((Roles::kRole == role ? 1 : 0) + ...);
        if (listed != (hasNightAction(role) ? 1 : 0)) {
            return false;
        }
    }
    return true;
}

static_assert(coversNightRoles(NightRoles{}),
              "у каждой роли с ночным ходом должен быть ровно один класс в NightRoles");

}  // namespace mafia
