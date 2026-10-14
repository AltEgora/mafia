// Тесты рассуждений ботов. Боты запускаются как корутины (CoroutineRuntime):
// так им можно доставить личное сообщение ведущего (Info), не заводя нитей.

#include <cstdint>
#include <optional>
#include <vector>

#include "mafia/bots.hpp"
#include "mafia/player_concepts.hpp"
#include "mafia/player_runtime.hpp"
#include "test_framework.hpp"

using namespace mafia;

namespace {

constexpr int kJournalist = 5;

struct Scenario {
    bool open = false;
    std::optional<Role> deadRole;  // при открытых объявлениях роль погибшего известна
    int mafiaTotal = 2;
    int ninjaTotal = 0;
};

// Журналист (игрок 5) сравнил игроков 1 и 2 — «одинаковый статус». Затем игрок 1
// погибает, и ведущий объявляет его «мафией». Что журналист скажет об игроке 2?
Response journalistAfterDeath(const Scenario& scenario, std::uint64_t seed) {
    std::vector<SharedPtr<Player>> players;
    for (int id = 1; id <= 6; ++id) {
        if (id == kJournalist) {
            players.push_back(makeBot<Journalist>(id, seed));
        } else {
            players.push_back(makeBot<Civilian>(id, seed));
        }
    }
    CoroutineRuntime runtime(CoroutineRuntime::readerFor(std::cin));
    runtime.start(players);
    runtime.send(kJournalist, Request{.type = RequestType::Info, .comparison = CompareResult{1, 2, true}});

    auto view = MakeShared<GameView>();
    view->day = 2;
    view->openAnnouncements = scenario.open;
    view->mafiaTotal = scenario.mafiaTotal;
    view->ninjaTotal = scenario.ninjaTotal;
    for (int id = 1; id <= 6; ++id) {
        view->players.push_back(PlayerPublicInfo{.id = id, .alive = id != 1});
    }
    PublicEvent death{.type = EventType::Death, .day = 2, .target = 1, .side = Side::Mafia};
    death.causes = {scenario.open ? DeathCause::Mafia : DeathCause::Unknown};
    death.role = scenario.deadRole;
    view->events.push_back(death);

    runtime.send(kJournalist, Request{.type = RequestType::Discussion, .view = view, .targets = {2, 3, 4, 6}});
    Response response = runtime.receive();
    runtime.stop();
    return response;
}

}  // namespace

// Ниндзя в составе нет: «мафия» при сравнении выглядела мафией, значит, игрок 2 — тоже.
TEST(ClosedModeWithoutNinjaTrustsAnnouncement) {
    for (std::uint64_t seed = 0; seed < 10; ++seed) {
        const Response response = journalistAfterDeath({.mafiaTotal = 2, .ninjaTotal = 0}, seed);
        CHECK(response.action == ActionType::Accuse);
        CHECK_EQ(response.target, 2);
        CHECK(response.confident);
    }
}

// Вся мафия — ниндзя: погибший при сравнении выглядел мирным, и игрок 2 тоже мирный.
TEST(ClosedModeTreatsNinjaAsTownLooking) {
    for (std::uint64_t seed = 0; seed < 10; ++seed) {
        const Response response = journalistAfterDeath({.mafiaTotal = 1, .ninjaTotal = 1}, seed);
        CHECK(!(response.action == ActionType::Accuse && response.target == 2));
    }
}

// Мафия из трёх, один из них ниндзя: погибший выглядел мафией с вероятностью 2/3 —
// подозрение к игроку 2 есть, но не уверенное.
TEST(ClosedModeWeighsPossibleNinja) {
    for (std::uint64_t seed = 0; seed < 10; ++seed) {
        const Response response = journalistAfterDeath({.mafiaTotal = 3, .ninjaTotal = 1}, seed);
        CHECK(response.action == ActionType::Accuse);
        CHECK_EQ(response.target, 2);
        CHECK(!response.confident);
    }
}

// Открытые объявления: роль известна, ниндзя при сравнении — мирный.
TEST(OpenModeUsesRevealedRole) {
    for (std::uint64_t seed = 0; seed < 10; ++seed) {
        const Response ninja = journalistAfterDeath({.open = true, .deadRole = Role::Ninja, .ninjaTotal = 1}, seed);
        CHECK(!(ninja.action == ActionType::Accuse && ninja.target == 2));

        const Response mafioso = journalistAfterDeath({.open = true, .deadRole = Role::Mafia}, seed);
        CHECK(mafioso.action == ActionType::Accuse && mafioso.target == 2 && mafioso.confident);
    }
}

MAFIA_TEST_MAIN()
