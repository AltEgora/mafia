#pragma once

// Минимальный тестовый каркас без сторонних библиотек: TEST, CHECK, CHECK_EQ,
// CHECK_THROWS_WITH. Каждый тестовый файл заканчивается MAFIA_TEST_MAIN().

#include <atomic>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace mafia_test {

inline std::atomic<int> failures{0};

struct TestCase {
    const char* name;
    void (*body)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*body)()) { registry().push_back({name, body}); }
};

template <class T>
std::string describe(const T& value) {
    if constexpr (requires(std::ostream& out) { out << value; }) {
        std::ostringstream out;
        out << value;
        return out.str();
    } else {
        return "<значение>";
    }
}

inline void fail(const char* file, int line, const std::string& message) {
    ++failures;
    std::cerr << file << ':' << line << ": " << message << '\n';
}

inline int runAll() {
    int failed = 0;
    for (const auto& test : registry()) {
        const int before = failures.load();
        try {
            test.body();
        } catch (const std::exception& error) {
            fail(__FILE__, __LINE__, std::string(test.name) + ": неожиданное исключение: " + error.what());
        }
        const bool ok = failures.load() == before;
        failed += ok ? 0 : 1;
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << test.name << '\n';
    }
    std::cout << registry().size() - static_cast<std::size_t>(failed) << '/' << registry().size()
              << " тестов пройдено\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace mafia_test

#define TEST(name)                                                         \
    static void name();                                                    \
    static const ::mafia_test::Registrar name##_registrar{#name, name};    \
    static void name()

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            ::mafia_test::fail(__FILE__, __LINE__, "CHECK(" #cond ") не выполнено");       \
        }                                                                                  \
    } while (false)

#define CHECK_EQ(actual, expected)                                                              \
    do {                                                                                        \
        /* копии, а не ссылки: (actual) может быть полем временного объекта */                 \
        const auto actualValue = (actual);                                                      \
        const auto expectedValue = (expected);                                                  \
        if (!(actualValue == expectedValue)) {                                                  \
            ::mafia_test::fail(__FILE__, __LINE__,                                              \
                               "CHECK_EQ(" #actual ", " #expected "): получено «" +             \
                                   ::mafia_test::describe(actualValue) + "», ожидалось «" +     \
                                   ::mafia_test::describe(expectedValue) + "»");                \
        }                                                                                       \
    } while (false)

// Выражение должно бросить Exception, в тексте которого есть substring.
#define CHECK_THROWS_WITH(expression, Exception, substring)                                          \
    do {                                                                                             \
        bool thrown = false;                                                                         \
        try {                                                                                        \
            (void)(expression);                                                                      \
        } catch (const Exception& error) {                                                           \
            thrown = true;                                                                           \
            if (std::string(error.what()).find(substring) == std::string::npos) {                    \
                ::mafia_test::fail(__FILE__, __LINE__,                                               \
                                   std::string("текст ошибки «") + error.what() + "» не содержит «" + \
                                       (substring) + "»");                                           \
            }                                                                                        \
        }                                                                                            \
        if (!thrown) {                                                                               \
            ::mafia_test::fail(__FILE__, __LINE__, "ожидалось исключение " #Exception " от " #expression); \
        }                                                                                            \
    } while (false)

#define MAFIA_TEST_MAIN() \
    int main() { return ::mafia_test::runAll(); }
