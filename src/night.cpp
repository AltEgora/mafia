#include "mafia/night.hpp"

#include <algorithm>
#include <ranges>

#include "mafia/ranges_util.hpp"

namespace mafia {

namespace {

Role roleAt(const std::vector<Role>& roles, int id) {
    return roles.at(static_cast<std::size_t>(id - 1));
}

}  // namespace

std::vector<int> NightContext::aliveExcept(int player) const {
    return allExcept(alive, player);
}

std::vector<int> NightContext::aliveOutsideTeam(Team team) const {
    return toVector(alive | std::views::filter([this, team](int player) {
                        return teamOf(roleAt(roles, player)) != team;
                    }));
}

NightOutcome resolveNight(const NightMoves& moves, const std::vector<Role>& roles) {
    NightOutcome outcome;

    // Мафия: большинство предложений; при равенстве — выбор босса, если он среди лидеров.
    if (!moves.mafia.empty()) {
        // Подсчёт голосов мафии: цель → число предложений.
        std::map<int, long> votes;
        for (int target : moves.mafia | std::views::transform(&NightMoves::Move::target)) {
            votes.try_emplace(target, std::ranges::count(moves.mafia, target, &NightMoves::Move::target));
        }
        const long best = std::ranges::max(votes | std::views::values);
        const std::vector<int> leaders =
            toVector(votes | std::views::filter([best](const auto& vote) { return vote.second == best; }) |
                     std::views::keys);
        const NightMoves::Move& boss = moves.mafia.front();
        const int victim = contains(leaders, boss.target) ? boss.target : leaders.front();
        outcome.mafiaVictim = victim;
        // Стреляет первый по номеру из голосовавших за жертву — босс, если он среди них.
        outcome.mafiaExecutor = std::ranges::find(moves.mafia, victim, &NightMoves::Move::target)->actor;
        outcome.attacks[victim].push_back(DeathCause::Mafia);
    }

    // Выстрелы комиссара и маньяка — по порядку номеров стрелявших.
    struct Shot {
        int actor;
        int target;
        DeathCause cause;
    };
    std::vector<Shot> shots;
    if (moves.commissar && moves.commissarShoots) {
        shots.push_back({moves.commissar->actor, moves.commissar->target, DeathCause::Commissar});
    }
    if (moves.maniac) {
        shots.push_back({moves.maniac->actor, moves.maniac->target, DeathCause::Maniac});
    }
    std::ranges::sort(shots, {}, &Shot::actor);
    for (const Shot& shot : shots) {
        outcome.attacks[shot.target].push_back(shot.cause);
    }

    if (moves.commissar && !moves.commissarShoots) {
        const int target = moves.commissar->target;
        outcome.check = CheckResult{target, checkSide(roleAt(roles, target)) == Side::Mafia};
    }
    if (moves.journalist) {
        const auto& [actor, first, second] = *moves.journalist;
        outcome.comparison =
            CompareResult{first, second, checkSide(roleAt(roles, first)) == checkSide(roleAt(roles, second))};
    }

    // Телохранитель отвечает одному из нападавших на подопечного.
    if (moves.bodyguard) {
        const auto attacked = outcome.attacks.find(moves.bodyguard->target);
        if (attacked != outcome.attacks.end()) {
            std::vector<int> attackers;
            for (DeathCause cause : attacked->second) {
                if (cause == DeathCause::Mafia && outcome.mafiaExecutor != 0) {
                    attackers.push_back(outcome.mafiaExecutor);
                } else if (cause == DeathCause::Maniac) {
                    attackers.push_back(moves.maniac->actor);
                }
            }
            if (!attackers.empty()) {
                const int shooter = std::ranges::min(attackers);
                outcome.counterKilled = shooter;
                outcome.attacks[shooter].push_back(DeathCause::Bodyguard);
            }
        }
    }

    for (const auto& [target, causes] : outcome.attacks) {
        if (moves.bodyguard && moves.bodyguard->target == target) {
            outcome.guarded[target] = causes;
        } else if (moves.doctor && moves.doctor->target == target) {
            outcome.healed[target] = causes;
        } else {
            outcome.deaths.push_back(target);
        }
    }
    return outcome;
}

}  // namespace mafia
