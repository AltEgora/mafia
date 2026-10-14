// Тесты помощников для std::ranges: toVector, contains, allExcept.

#include <concepts>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "mafia/night.hpp"
#include "mafia/ranges_util.hpp"
#include "test_framework.hpp"

using mafia::allExcept;
using mafia::contains;
using mafia::toVector;

TEST(ToVectorCopiesSizedRanges) {
    const std::vector<int> source = {3, 1, 2};
    CHECK(toVector(source) == source);

    auto numbers = std::views::iota(1, 6);
    static_assert(std::ranges::sized_range<decltype(numbers)>);
    CHECK(toVector(numbers) == (std::vector<int>{1, 2, 3, 4, 5}));
}

// Тип элементов берётся из диапазона; размер заранее неизвестен.
TEST(ToVectorFromFilterAndTransform) {
    auto words = std::views::iota(1, 7) | std::views::filter([](int n) { return n % 2 == 0; }) |
                 std::views::transform([](int n) { return std::to_string(n); });
    static_assert(!std::ranges::sized_range<decltype(words)>);
    const auto result = toVector(words);
    static_assert(std::same_as<decltype(result), const std::vector<std::string>>);
    CHECK(result == (std::vector<std::string>{"2", "4", "6"}));
}

TEST(ToVectorFromSinglePassRange) {
    std::istringstream in("5 8 13");
    auto numbers = std::views::istream<int>(in);
    static_assert(std::ranges::input_range<decltype(numbers)> && !std::ranges::forward_range<decltype(numbers)>);
    CHECK(toVector(numbers) == (std::vector<int>{5, 8, 13}));
}

TEST(ToVectorFromTemporaryAndEmpty) {
    CHECK(toVector(std::vector<int>{4, 2}) == (std::vector<int>{4, 2}));
    CHECK(toVector(std::views::empty<int>).empty());
    CHECK(toVector(std::views::iota(3, 3)).empty());
}

TEST(ContainsValues) {
    const std::vector<int> numbers = {4, 8, 15};
    CHECK(contains(numbers, 8));
    CHECK(!contains(numbers, 16));
    CHECK(!contains(std::vector<int>{}, 1));
    CHECK(contains(std::views::iota(1, 100), 99));

    const std::initializer_list<std::string_view> commands = {"п", "в", "check"};
    CHECK(contains(commands, std::string_view("в")));
    CHECK(!contains(commands, std::string_view("x")));
}

TEST(ContainsWithProjection) {
    const std::vector<mafia::NightMoves::Move> moves = {{1, 7}, {2, 9}};
    CHECK(contains(moves, 9, &mafia::NightMoves::Move::target));
    CHECK(!contains(moves, 1, &mafia::NightMoves::Move::target));
    CHECK(contains(moves, 1, &mafia::NightMoves::Move::actor));
}

// Однопроходный диапазон читается один раз и только до первого совпадения.
TEST(ContainsStopsAtFirstMatchOnSinglePassRange) {
    std::istringstream in("5 8 13 21");
    auto numbers = std::views::istream<int>(in);
    CHECK(contains(numbers, 8));
    int next = 0;
    in >> next;
    CHECK_EQ(next, 13);  // дальше 8 поиск не читал
}

TEST(AllExceptKeepsOrder) {
    CHECK(allExcept(std::vector<int>{5, 3, 5, 1}, 5) == (std::vector<int>{3, 1}));
    const std::vector<int> alive = {1, 2, 3};
    CHECK(allExcept(alive, 7) == alive);
    CHECK(allExcept(alive, 2) == (std::vector<int>{1, 3}));
    CHECK(alive == (std::vector<int>{1, 2, 3}));  // исходный диапазон не меняется
    CHECK(allExcept(std::vector<std::string>{"a", "b", "a"}, std::string("a")) == (std::vector<std::string>{"b"}));
    CHECK(allExcept(std::views::iota(1, 5), 4) == (std::vector<int>{1, 2, 3}));
}

MAFIA_TEST_MAIN()
