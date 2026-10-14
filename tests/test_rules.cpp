// Тесты правил партии: подсчёт лагерей, победитель, итоги голосования.

#include <map>
#include <ranges>
#include <sstream>
#include <utility>
#include <vector>

#include "mafia/game_rules.hpp"
#include "test_framework.hpp"

using mafia::countTeams;
using mafia::decideWinner;
using mafia::Response;
using mafia::Role;
using mafia::TeamCounts;
using mafia::VoteRow;
using mafia::Winner;

namespace {

// Бюллетени «голосующий → цель».
std::map<int, Response> ballots(std::initializer_list<std::pair<int, int>> votes) {
    std::map<int, Response> result;
    for (const auto& [voter, target] : votes) {
        result[voter].action = mafia::ActionType::Vote;
        result[voter].target = target;
    }
    return result;
}

}  // namespace

// ------------------------------------------------------------- countTeams

TEST(CountTeamsByCamp) {
    const std::vector<Role> roles = {Role::Mafia,  Role::Mafia,      Role::Ninja,     Role::Commissar, Role::Doctor,
                                     Role::Maniac, Role::Journalist, Role::Bodyguard, Role::Civilian};
    CHECK(countTeams(roles) == (TeamCounts{.town = 5, .mafia = 3, .maniac = 1}));
    CHECK(countTeams(std::vector<Role>{}) == TeamCounts{});
}

// Однопроходный диапазон: роли читаются из потока. Функция обязана обойти его
// один раз — при трёх проходах второй и третий увидели бы пустой поток.
TEST(CountTeamsReadsSinglePassRangeOnce) {
    std::istringstream in("1 7 0 4 2 0");  // мафия, ниндзя, мирный, маньяк, комиссар, мирный
    auto roles = std::views::istream<int>(in) | std::views::transform([](int value) { return static_cast<Role>(value); });
    static_assert(std::ranges::input_range<decltype(roles)> && !std::ranges::forward_range<decltype(roles)>);
    CHECK(countTeams(roles) == (TeamCounts{.town = 3, .mafia = 2, .maniac = 1}));
}

// Как у ведущего: номера живых → роли.
TEST(CountTeamsFromTransformedIds) {
    const std::vector<Role> roles = {Role::Civilian, Role::Mafia, Role::Maniac, Role::Doctor};
    const std::vector<int> alive = {1, 2, 4};
    auto aliveRoles = alive | std::views::transform([&roles](int id) { return roles[static_cast<std::size_t>(id - 1)]; });
    CHECK(countTeams(aliveRoles) == (TeamCounts{.town = 2, .mafia = 1, .maniac = 0}));
}

// ----------------------------------------------------------- decideWinner

TEST(DecideWinnerFollowsRules) {
    CHECK(decideWinner(TeamCounts{.town = 3, .mafia = 0, .maniac = 0}) == Winner::Town);
    CHECK(decideWinner(TeamCounts{.town = 0, .mafia = 0, .maniac = 0}) == Winner::Draw);
    CHECK(decideWinner(TeamCounts{.town = 1, .mafia = 0, .maniac = 1}) == Winner::Maniac);  // один на один
    CHECK(decideWinner(TeamCounts{.town = 0, .mafia = 0, .maniac = 1}) == Winner::Maniac);
    CHECK(!decideWinner(TeamCounts{.town = 2, .mafia = 0, .maniac = 1}));
    CHECK(decideWinner(TeamCounts{.town = 2, .mafia = 2, .maniac = 0}) == Winner::Mafia);  // поровну
    CHECK(decideWinner(TeamCounts{.town = 1, .mafia = 2, .maniac = 0}) == Winner::Mafia);
    CHECK(!decideWinner(TeamCounts{.town = 3, .mafia = 2, .maniac = 0}));
    CHECK(!decideWinner(TeamCounts{.town = 1, .mafia = 2, .maniac = 1}));  // равенство при живом маньяке
    CHECK(decideWinner(TeamCounts{.town = 0, .mafia = 2, .maniac = 1}) == Winner::Mafia);
    CHECK(!decideWinner(TeamCounts{.town = 0, .mafia = 1, .maniac = 1}));
}

// -------------------------------------------------------- итоги голосования

TEST(TallyVotesGroupsVotersAndSortsRows) {
    const auto rows = mafia::tallyVotes(ballots({{1, 3}, {2, 5}, {3, 5}, {4, 3}, {5, 1}, {6, 5}}));
    const std::vector<VoteRow> expected = {{5, {2, 3, 6}}, {3, {1, 4}}, {1, {5}}};
    CHECK(rows == expected);
    CHECK(mafia::voteLeaders(rows) == std::vector<int>{5});
}

// При равенстве голосов строки и лидеры идут по возрастанию номера цели.
TEST(TallyVotesTieKeepsTargetOrder) {
    const auto rows = mafia::tallyVotes(ballots({{1, 4}, {2, 2}, {3, 4}, {4, 2}, {5, 1}}));
    const std::vector<VoteRow> expected = {{2, {2, 4}}, {4, {1, 3}}, {1, {5}}};
    CHECK(rows == expected);
    CHECK(mafia::voteLeaders(rows) == (std::vector<int>{2, 4}));
}

TEST(TallyVotesEveryoneDifferent) {
    const auto rows = mafia::tallyVotes(ballots({{1, 2}, {2, 3}, {3, 1}}));
    CHECK(rows == (std::vector<VoteRow>{{1, {3}}, {2, {1}}, {3, {2}}}));
    CHECK(mafia::voteLeaders(rows) == (std::vector<int>{1, 2, 3}));
}

TEST(TallyVotesEmpty) {
    CHECK(mafia::tallyVotes({}).empty());
    CHECK(mafia::voteLeaders({}).empty());
}

MAFIA_TEST_MAIN()
