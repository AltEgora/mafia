#include "mafia/game_rules.hpp"

#include <functional>

#include "mafia/ranges_util.hpp"

namespace mafia {

std::optional<Winner> decideWinner(const TeamCounts& alive) {
    if (alive.mafia == 0 && alive.maniac == 0) {
        return alive.town > 0 ? Winner::Town : Winner::Draw;
    }
    if (alive.mafia == 0) {
        // Маньяк побеждает, оставшись один на один с мирным.
        return alive.town <= 1 ? std::optional(Winner::Maniac) : std::nullopt;
    }
    if (alive.maniac == 0) {
        return alive.mafia >= alive.town ? std::optional(Winner::Mafia) : std::nullopt;
    }
    // Жив маньяк: при равенстве игра продолжается до смерти мафии или маньяка.
    return alive.mafia > alive.town + alive.maniac ? std::optional(Winner::Mafia) : std::nullopt;
}

std::vector<VoteRow> tallyVotes(const std::map<int, Response>& allBallots) {
    // Воздержавшиеся (так ходит только ИИ-агент) не голосуют ни против кого.
    auto ballots = allBallots | std::views::filter([](const auto& ballot) {
                       return ballot.second.action != ActionType::Abstain;
                   });
    // Цели, за которые голосовали, — по возрастанию номеров, без повторов.
    std::vector<int> targets = toVector(ballots | std::views::values | std::views::transform(&Response::target));
    std::ranges::sort(targets);
    const auto repeated = std::ranges::unique(targets);
    targets.erase(repeated.begin(), repeated.end());

    // Голосовавшие против цели — по возрастанию номеров (ballots упорядочен по голосующим).
    const auto votersAgainst = [&ballots](int target) {
        return toVector(ballots |
                        std::views::filter([target](const auto& ballot) { return ballot.second.target == target; }) |
                        std::views::keys);
    };
    std::vector<VoteRow> rows = toVector(
        targets | std::views::transform([&votersAgainst](int target) { return VoteRow{target, votersAgainst(target)}; }));

    // По убыванию голосов; stable_sort сохраняет порядок номеров при равенстве.
    std::ranges::stable_sort(rows, std::ranges::greater{}, [](const VoteRow& row) { return row.voters.size(); });
    return rows;
}

std::vector<int> voteLeaders(const std::vector<VoteRow>& rows) {
    if (rows.empty()) {
        return {};
    }
    const std::size_t top = rows.front().voters.size();
    return toVector(rows | std::views::take_while([top](const VoteRow& row) { return row.voters.size() == top; }) |
                    std::views::transform(&VoteRow::target));
}

}  // namespace mafia
