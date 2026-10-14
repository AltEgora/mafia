#pragma once

// Помощники для std::ranges.

#include <algorithm>
#include <functional>
#include <iterator>
#include <ranges>
#include <vector>

namespace mafia {

// Собирает диапазон в вектор (std::ranges::to появится только в C++23).
template <std::ranges::input_range Range>
auto toVector(Range&& range) {
    std::vector<std::ranges::range_value_t<Range>> result;
    if constexpr (std::ranges::sized_range<Range>) {
        result.reserve(static_cast<std::size_t>(std::ranges::size(range)));
    }
    std::ranges::copy(range, std::back_inserter(result));
    return result;
}

// Элементы диапазона, кроме равных value, — в исходном порядке. Диапазон
// передаётся в views::filter как lvalue: временный аргумент живёт до конца
// вызова, а toVector успевает всё скопировать.
template <std::ranges::input_range Range, class Value>
    requires std::equality_comparable_with<std::ranges::range_reference_t<Range>, const Value&>
auto allExcept(Range&& range, const Value& value) {
    return toVector(range | std::views::filter([&value](const auto& item) { return !(item == value); }));
}

// Есть ли значение в диапазоне (std::ranges::contains появится только в C++23).
// Диапазон проходится один раз, поэтому подходит и однопроходный.
template <std::ranges::input_range Range, class Value, class Projection = std::identity>
    requires std::indirect_binary_predicate<std::ranges::equal_to,
                                            std::projected<std::ranges::iterator_t<Range>, Projection>, const Value*>
bool contains(Range&& range, const Value& value, Projection projection = {}) {
    const auto last = std::ranges::end(range);
    return std::ranges::find(std::ranges::begin(range), last, value, std::move(projection)) != last;
}

}  // namespace mafia
