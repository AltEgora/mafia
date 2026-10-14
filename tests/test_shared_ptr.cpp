// Тесты SharedPtr без сторонних фреймворков.

#include <atomic>
#include <iostream>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mafia/shared_ptr.hpp"

using mafia::MakeShared;
using mafia::SharedPtr;

namespace {

std::atomic<int> g_failures{0};

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            ++g_failures;                                                                 \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK(" #cond ") не выполнено\n"; \
        }                                                                                 \
    } while (false)

struct TestCase {
    const char* name;
    void (*body)();
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*body)()) { registry().push_back({name, body}); }
};

#define TEST(name)                               \
    void name();                                 \
    const Registrar name##_registrar{#name, name}; \
    void name()

// Считает живые экземпляры, чтобы ловить утечки и двойное удаление.
struct Tracked {
    static inline std::atomic<int> alive{0};
    int value;
    explicit Tracked(int v = 0) : value(v) { ++alive; }
    Tracked(const Tracked&) = delete;
    ~Tracked() { --alive; }
};

// Невиртуальный деструктор: SharedPtr<Base> всё равно должен вызвать ~Derived.
struct Base {
    static inline int destroyed = 0;
    int base = 1;
    ~Base() { ++destroyed; }
};

struct Derived : Base {
    static inline int destroyed = 0;
    int derived = 2;
    ~Derived() { ++destroyed; }
};

struct Animal {
    virtual ~Animal() = default;
    virtual int legs() const { return 0; }
};

struct Cat : Animal {
    int legs() const override { return 4; }
};

TEST(EmptyPointer) {
    SharedPtr<int> a;
    SharedPtr<int> b = nullptr;
    CHECK(a.get() == nullptr);
    CHECK(!a);
    CHECK(a.use_count() == 0);
    CHECK(a == nullptr);
    CHECK(a == b);
}

TEST(OwnsAndDeletes) {
    {
        SharedPtr<Tracked> p(new Tracked(5));
        CHECK(Tracked::alive == 1);
        CHECK(p.use_count() == 1);
        CHECK(p->value == 5);
        CHECK((*p).value == 5);
        CHECK(static_cast<bool>(p));
    }
    CHECK(Tracked::alive == 0);
}

TEST(CopyConstructionShares) {
    SharedPtr<Tracked> a(new Tracked(1));
    {
        SharedPtr<Tracked> b = a;
        CHECK(a.use_count() == 2);
        CHECK(b.get() == a.get());
        b->value = 7;
    }
    CHECK(a.use_count() == 1);
    CHECK(a->value == 7);
    a.reset();
    CHECK(Tracked::alive == 0);
}

TEST(MoveConstructionTransfers) {
    SharedPtr<Tracked> a(new Tracked(3));
    Tracked* raw = a.get();
    SharedPtr<Tracked> b = std::move(a);
    CHECK(a.get() == nullptr);
    CHECK(a.use_count() == 0);
    CHECK(b.get() == raw);
    CHECK(b.use_count() == 1);
}

TEST(CopyAssignment) {
    SharedPtr<Tracked> a(new Tracked(1));
    SharedPtr<Tracked> b(new Tracked(2));
    b = a;  // старый объект b должен удалиться
    CHECK(Tracked::alive == 1);
    CHECK(a.use_count() == 2);
    CHECK(b->value == 1);

    auto& alias = b;
    b = alias;  // самоприсваивание
    CHECK(b.use_count() == 2);
    CHECK(b->value == 1);
}

TEST(MoveAssignment) {
    SharedPtr<Tracked> a(new Tracked(1));
    SharedPtr<Tracked> b(new Tracked(2));
    b = std::move(a);
    CHECK(Tracked::alive == 1);
    CHECK(!a);
    CHECK(b->value == 1);
    CHECK(b.use_count() == 1);

    auto& alias = b;
    b = std::move(alias);  // самоперемещение не должно терять объект
    CHECK(b.use_count() == 1);
    CHECK(b->value == 1);

    b = nullptr;
    CHECK(Tracked::alive == 0);
}

TEST(ConvertDerivedToBase) {
    Base::destroyed = 0;
    Derived::destroyed = 0;
    {
        SharedPtr<Derived> d(new Derived);
        SharedPtr<Base> b = d;  // конвертирующий конструктор копирования
        CHECK(b.use_count() == 2);
        CHECK(b->base == 1);
        SharedPtr<Base> moved = std::move(d);  // конвертирующий конструктор перемещения
        CHECK(!d);
        CHECK(moved.use_count() == 2);
        SharedPtr<Base> assigned;
        assigned = moved;  // конвертирующее присваивание не нужно, но проверим обычное
        CHECK(assigned.use_count() == 3);
    }
    CHECK(Derived::destroyed == 1);
    CHECK(Base::destroyed == 1);

    Derived::destroyed = 0;
    {
        SharedPtr<Base> b(new Derived);  // хранится Derived*, удаляется как Derived
    }
    CHECK(Derived::destroyed == 1);

    SharedPtr<Animal> animal = MakeShared<Cat>();
    CHECK(animal->legs() == 4);
    SharedPtr<Animal> other;
    other = MakeShared<Cat>();  // конвертирующее перемещающее присваивание
    CHECK(other->legs() == 4);
    SharedPtr<const Animal> constAnimal = animal;
    CHECK(constAnimal->legs() == 4);
    CHECK(animal.use_count() == 2);
}

TEST(Reset) {
    SharedPtr<Tracked> a(new Tracked(1));
    SharedPtr<Tracked> b = a;
    a.reset(new Tracked(2));
    CHECK(Tracked::alive == 2);
    CHECK(a->value == 2);
    CHECK(b.use_count() == 1);
    b.reset();
    CHECK(!b);
    CHECK(Tracked::alive == 1);
    a.reset();
    CHECK(Tracked::alive == 0);
}

TEST(Swap) {
    SharedPtr<Tracked> a(new Tracked(1));
    SharedPtr<Tracked> b(new Tracked(2));
    SharedPtr<Tracked> c = b;
    a.swap(b);
    CHECK(a->value == 2);
    CHECK(b->value == 1);
    CHECK(a.use_count() == 2);
    CHECK(b.use_count() == 1);
    swap(a, b);
    CHECK(a->value == 1);
    CHECK(b->value == 2);
}

TEST(Comparisons) {
    auto a = MakeShared<int>(1);
    auto b = MakeShared<int>(1);
    SharedPtr<int> a2 = a;
    SharedPtr<int> empty;

    CHECK(a == a2);
    CHECK(a != b);
    CHECK((a < b) == (a.get() < b.get()));
    CHECK((a <=> b) == std::compare_three_way{}(a.get(), b.get()));
    CHECK(a <= a2);
    CHECK(a >= a2);
    CHECK(empty == nullptr);
    CHECK(nullptr == empty);
    CHECK(a != nullptr);
    CHECK(a > nullptr);

    SharedPtr<Derived> d = MakeShared<Derived>();
    SharedPtr<Base> asBase = d;
    CHECK(asBase == d);  // сравнение указателей разных типов

    std::unordered_set<SharedPtr<int>> set{a, a2, b};
    CHECK(set.size() == 2);
}

TEST(CustomDeleter) {
    int calls = 0;
    {
        SharedPtr<int> p(new int(42), [&calls](int* raw) {
            ++calls;
            delete raw;
        });
        SharedPtr<int> copy = p;
        CHECK(*copy == 42);
    }
    CHECK(calls == 1);
}

TEST(MakeSharedConstructsInPlace) {
    {
        auto p = MakeShared<Tracked>(11);
        CHECK(p->value == 11);
        CHECK(p.use_count() == 1);
        CHECK(Tracked::alive == 1);
    }
    CHECK(Tracked::alive == 0);

    auto pair = MakeShared<std::pair<int, double>>(1, 2.5);
    CHECK(pair->first == 1);
    CHECK(pair->second == 2.5);
}

// Одновременное копирование одного указателя из многих нитей:
// счётчик должен вернуться к 1, объект удалиться ровно один раз.
TEST(ConcurrentCopies) {
    constexpr int kThreads = 8;
    constexpr int kIterations = 50000;
    std::atomic<int> wrongValues{0};
    {
        const auto shared = MakeShared<Tracked>(42);
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&shared, &wrongValues] {
                for (int i = 0; i < kIterations; ++i) {
                    SharedPtr<Tracked> copy = shared;
                    SharedPtr<Tracked> moved = std::move(copy);
                    if (moved->value != 42) {
                        ++wrongValues;
                    }
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        CHECK(shared.use_count() == 1);
    }
    CHECK(wrongValues == 0);
    CHECK(Tracked::alive == 0);
}

// Последним владельцем оказывается рабочая нить, а не главная.
TEST(LastOwnerInAnotherThread) {
    auto p = MakeShared<Tracked>(1);
    std::atomic<int> sum{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([copy = p, &sum]() mutable {
            sum += copy->value;  // только чтение объекта в чужой нити
            copy.reset();
        });
    }
    p.reset();
    for (auto& thread : threads) {
        thread.join();
    }
    CHECK(sum == 4);
    CHECK(Tracked::alive == 0);
}

}  // namespace

int main() {
    int failedTests = 0;
    for (const auto& test : registry()) {
        const int before = g_failures.load();
        test.body();
        const bool ok = g_failures.load() == before;
        if (!ok) {
            ++failedTests;
        }
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << test.name << '\n';
    }
    std::cout << registry().size() - static_cast<std::size_t>(failedTests) << '/' << registry().size()
              << " тестов пройдено\n";
    return failedTests == 0 ? 0 : 1;
}
