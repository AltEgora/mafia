#pragma once

// Концепты типов игроков и шаблоны, которые управляют ходами.
//
//   DayPlayer   — участник дня: discuss() и vote();
//   NightPlayer — вдобавок ночной ход act(); мирный житель им НЕ является;
//   RoleClass   — класс конкретной роли: константа T::kRole;
//   NightRole   — класс роли с ночным ходом: act() и правила хода
//                 (nightTargets, isValidNightMove, fallbackNightMove, recordNightMove);
//   RoleBot     — класс роли бота, создаваемый из (номер, сид).
//
// Концепты ограничивают:
//   playTurn / playDayTurn / playNightTurn — ход в нити игрока (здесь);
//   makeBot                                — создание ботов ведущим (здесь);
//   visitNightRole, collectNightMove       — управление ночью у ведущего (role_dispatch.hpp).

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "mafia/messages.hpp"
#include "mafia/night.hpp"
#include "mafia/player.hpp"
#include "mafia/random.hpp"
#include "mafia/roles.hpp"
#include "mafia/shared_ptr.hpp"

namespace mafia {

// Конкретный (не абстрактный) игрок, который умеет высказаться и проголосовать.
template <class T>
concept DayPlayer = std::derived_from<T, Player> && !std::is_abstract_v<T> &&
                    requires(T& player, const T& constPlayer, const Request& request) {
                        { constPlayer.id() } -> std::same_as<int>;
                        { constPlayer.role() } -> std::same_as<Role>;
                        { player.discuss(request) } -> std::same_as<Response>;
                        { player.vote(request) } -> std::same_as<Response>;
                    };

// Игрок с ночным ходом. Определён через DayPlayer, поэтому уточняет его:
// из двух перегрузок playTurn для него выбирается ночная.
template <class T>
concept NightPlayer = DayPlayer<T> && requires(T& player, const Request& request) {
    { player.act(request) } -> std::same_as<Response>;
};

// Класс конкретной роли: роль известна на этапе компиляции.
template <class T>
concept RoleClass = DayPlayer<T> && requires {
    { T::kRole } -> std::convertible_to<Role>;
};

// Класс роли с ночным ходом: сам знает правила хода, по которым ведущий
// раздаёт цели, проверяет ответ, ходит за бота, если тот ответил неверно,
// и собирает ответ в ходы ночи.
template <class T>
concept NightRole = RoleClass<T> && NightPlayer<T> &&
                    requires(const NightContext& context, int self, const Request& request,
                             const Response& response, Rng& rng, NightMoves& moves) {
                        { T::nightTargets(context, self) } -> std::same_as<std::vector<int>>;
                        { T::isValidNightMove(response, request) } -> std::same_as<bool>;
                        { T::fallbackNightMove(request, rng) } -> std::same_as<Response>;
                        { T::recordNightMove(moves, self, response) } -> std::same_as<void>;
                    };

// Класс роли бота: создаётся из номера игрока и сида генератора.
template <class T>
concept RoleBot = RoleClass<T> && std::constructible_from<T, int, std::uint64_t>;

// ------------------------------------------------------- ход в нити игрока

// Дневная часть хода — для любого участника дня.
template <DayPlayer P>
Response playDayTurn(P& player, const Request& request) {
    switch (request.type) {
        case RequestType::Discussion:
            return player.discuss(request);
        case RequestType::DayVote:
            return player.vote(request);
        default:
            return Response{.valid = false, .error = "этот запрос не относится ко дню"};
    }
}

// Ночной ход доступен только игрокам с act(): для мирного жителя этот вызов не скомпилируется.
template <NightPlayer P>
Response playNightTurn(P& player, const Request& request) {
    return player.act(request);
}

// Ход участника без ночной роли: ночью он спит.
template <DayPlayer P>
Response playTurn(P& player, const Request& request) {
    if (request.type == RequestType::Night) {
        return Response{.valid = false, .error = "у этой роли нет ночного хода"};
    }
    return playDayTurn(player, request);
}

// Ход игрока с ночной ролью (более ограниченная перегрузка — выбирается для NightPlayer).
template <NightPlayer P>
Response playTurn(P& player, const Request& request) {
    if (request.type == RequestType::Night) {
        return playNightTurn(player, request);
    }
    return playDayTurn(player, request);
}

// -------------------------------------------------------- создание ботов

// Ведущий создаёт ботов только через эту функцию: класс без нужных методов
// не скомпилируется (см. tests/concept_violation.cpp).
template <RoleBot Bot>
SharedPtr<Bot> makeBot(int id, std::uint64_t seed) {
    return MakeShared<Bot>(id, seed);
}

}  // namespace mafia
