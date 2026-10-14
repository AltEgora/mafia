#pragma once

// Правила партии в виде чистых функций: подсчёт лагерей, победитель, итоги голосования.

#include <algorithm>
#include <concepts>
#include <map>
#include <optional>
#include <ranges>
#include <vector>

#include "mafia/messages.hpp"
#include "mafia/roles.hpp"

namespace mafia {

enum class Winner { Town, Mafia, Maniac, Draw, Aborted };

// Сколько живых в каждом лагере.
struct TeamCounts {
    int town = 0;
    int mafia = 0;  // вместе с ниндзя
    int maniac = 0;

    bool operator==(const TeamCounts&) const = default;
};

// Считает лагеря за один проход, поэтому подходит любой input_range — в том
// числе однопроходный (например, роли, читаемые из потока).
template <std::ranges::input_range Roles>
    requires std::convertible_to<std::ranges::range_reference_t<Roles>, Role>
TeamCounts countTeams(Roles&& aliveRoles) {
    TeamCounts counts;
    std::ranges::for_each(
        aliveRoles,
        [&counts](Team team) {
            switch (team) {
                case Team::Town:
                    ++counts.town;
                    break;
                case Team::Mafia:
                    ++counts.mafia;
                    break;
                case Team::Maniac:
                    ++counts.maniac;
                    break;
            }
        },
        [](Role role) { return teamOf(role); });
    return counts;
}

// Победитель по числу живых в лагерях; nullopt — игра продолжается.
std::optional<Winner> decideWinner(const TeamCounts& alive);

// Строка итогов голосования: за кого и кто голосовал (по возрастанию номеров).
struct VoteRow {
    int target = 0;
    std::vector<int> voters;

    bool operator==(const VoteRow&) const = default;
};

// Итоги голосования по бюллетеням (голосующий → ответ с целью): по убыванию
// числа голосов, при равенстве — по возрастанию номера цели. Воздержавшиеся
// (ActionType::Abstain) не учитываются; если воздержались все, строк нет.
std::vector<VoteRow> tallyVotes(const std::map<int, Response>& ballots);

// Цели с наибольшим числом голосов (по возрастанию номеров); строки — из tallyVotes.
std::vector<int> voteLeaders(const std::vector<VoteRow>& rows);

}  // namespace mafia
