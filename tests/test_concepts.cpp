// Тесты концептов игроков и шаблонов, которые они ограничивают.
// Основная часть проверяется при компиляции (static_assert): неподходящие классы
// концепты отвергают, а ограниченные ими шаблоны (playTurn, playNightTurn,
// visitNightRole, makeBot) для таких классов недоступны. Во время выполнения
// проверяем, что ходы и правила ролей работают через эти шаблоны.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "mafia/bots.hpp"
#include "mafia/human_player.hpp"
#include "mafia/player_concepts.hpp"
#include "mafia/ranges_util.hpp"
#include "mafia/role_dispatch.hpp"
#include "test_framework.hpp"

using namespace mafia;

namespace {

// ------------------------------------------------------ неподходящие классы

// Не наследник Player — даже со всеми методами это не игрок.
struct NotAPlayer {
    [[maybe_unused]] static constexpr Role kRole = Role::Civilian;
    NotAPlayer(int, std::uint64_t) {}
    Response discuss(const Request&) { return {}; }
    Response vote(const Request&) { return {}; }
    Response act(const Request&) { return {}; }
};

// Общая заготовка для «почти игроков»: конкретный наследник Player.
class Stub : public Player {
public:
    static constexpr Role kRole = Role::Civilian;
    Stub(int id, std::uint64_t) : Player(id, kRole) {}

protected:
    Response handle(const Request&) override { return {}; }
};

// Нет vote().
class NoVote final : public Stub {
public:
    using Stub::Stub;
    Response discuss(const Request&) { return {}; }
    Response act(const Request&) { return {}; }
};

// vote() возвращает не Response.
class WrongVoteType final : public Stub {
public:
    using Stub::Stub;
    Response discuss(const Request&) { return {}; }
    int vote(const Request&) { return 0; }
};

// vote() есть, но недоступен снаружи.
class HiddenVote final : public Stub {
public:
    using Stub::Stub;
    Response discuss(const Request&) { return {}; }

protected:
    Response vote(const Request&) { return {}; }
};

// Абстрактный класс со всеми методами, но без handle().
class Abstract : public Player {
public:
    static constexpr Role kRole = Role::Civilian;
    Abstract(int id, std::uint64_t) : Player(id, kRole) {}
    Response discuss(const Request&) { return {}; }
    Response vote(const Request&) { return {}; }
    Response act(const Request&) { return {}; }
};

// Участник дня без ночного хода — как мирный житель.
class DayOnly final : public Stub {
public:
    using Stub::Stub;
    Response discuss(const Request&) { return {}; }
    Response vote(const Request&) { return {}; }
};

// Ночной бот без статических правил хода.
class NoRules final : public NightBot {
public:
    static constexpr Role kRole = Role::Maniac;
    NoRules(int id, std::uint64_t seed) : NightBot(id, kRole, seed) {}

protected:
    Response handle(const Request& request) override { return playTurn(*this, request); }
    Response chooseNightAction(const Request&) override { return {}; }
};

// Правила хода есть, а самого ночного хода (act) нет.
class RulesWithoutAct final : public BotPlayer {
public:
    static constexpr Role kRole = Role::Doctor;
    RulesWithoutAct(int id, std::uint64_t seed) : BotPlayer(id, kRole, seed) {}
    static std::vector<int> nightTargets(const NightContext&, int) { return {}; }
    static bool isValidNightMove(const Response&, const Request&) { return true; }
    static Response fallbackNightMove(const Request&, Rng&) { return {}; }
    static void recordNightMove(NightMoves&, int, const Response&) {}

protected:
    Response handle(const Request& request) override { return playTurn(*this, request); }
};

// Правило хода с неверной сигнатурой: проверка возвращает int, а не bool.
class WrongRuleType final : public NightBot {
public:
    static constexpr Role kRole = Role::Doctor;
    WrongRuleType(int id, std::uint64_t seed) : NightBot(id, kRole, seed) {}
    static std::vector<int> nightTargets(const NightContext&, int) { return {}; }
    static int isValidNightMove(const Response&, const Request&) { return 1; }
    static Response fallbackNightMove(const Request&, Rng&) { return {}; }
    static void recordNightMove(NightMoves&, int, const Response&) {}

protected:
    Response handle(const Request& request) override { return playTurn(*this, request); }
    Response chooseNightAction(const Request&) override { return {}; }
};

// Ночной бот без константы kRole.
class Anonymous final : public NightBot {
public:
    Anonymous(int id, std::uint64_t seed) : NightBot(id, Role::Maniac, seed) {}

protected:
    Response handle(const Request& request) override { return playTurn(*this, request); }
    Response chooseNightAction(const Request&) override { return {}; }
};

// Класс роли, которого нельзя создать из (номер, сид).
class NeedsTeam final : public BotPlayer {
public:
    static constexpr Role kRole = Role::Civilian;
    NeedsTeam(int id, std::uint64_t seed, std::vector<int> team)
        : BotPlayer(id, kRole, seed), team_(std::move(team)) {}

protected:
    Response handle(const Request& request) override { return playTurn(*this, request); }

private:
    std::vector<int> team_;
};

// --------------------------------------- доступность ограниченных шаблонов

template <class T>
constexpr bool canMakeBot = requires { makeBot<T>(1, std::uint64_t{0}); };

template <class T>
constexpr bool canPlayTurn = requires(T& player, const Request& request) { playTurn(player, request); };

template <class T>
constexpr bool canPlayNight = requires(T& player, const Request& request) { playNightTurn(player, request); };

struct NoopVisitor {
    template <class R>
    void operator()() const {}
};

template <class T>
constexpr bool canDriveNight = requires { visitNightRole(RoleList<T>{}, Role::Civilian, NoopVisitor{}); };

// Концепты отвергают неподходящие классы...
static_assert(!DayPlayer<NotAPlayer>, "класс должен наследоваться от Player");
static_assert(!DayPlayer<NoVote>, "без vote() нельзя участвовать в дне");
static_assert(!DayPlayer<WrongVoteType>, "vote() обязан возвращать Response");
static_assert(!DayPlayer<HiddenVote>, "vote() должен быть публичным");
static_assert(!DayPlayer<Abstract>, "абстрактный класс не может играть");
static_assert(DayPlayer<DayOnly> && !NightPlayer<DayOnly>, "без act() нет ночного хода");
static_assert(NightPlayer<NoRules> && RoleClass<NoRules> && !NightRole<NoRules>, "ночной роли нужны правила хода");
static_assert(DayPlayer<RulesWithoutAct> && !NightRole<RulesWithoutAct>, "правила без act() — не ночная роль");
static_assert(!NightRole<WrongRuleType>, "isValidNightMove обязан возвращать bool");
static_assert(NightPlayer<Anonymous> && !RoleClass<Anonymous> && !NightRole<Anonymous>, "классу роли нужна kRole");
static_assert(RoleClass<NeedsTeam> && !RoleBot<NeedsTeam>, "класс роли бота создаётся из (номер, сид)");

// ...и ограниченные ими шаблоны для таких классов недоступны.
static_assert(!canPlayTurn<NoVote> && !canPlayTurn<NotAPlayer> && canPlayTurn<DayOnly>);
static_assert(!canPlayNight<DayOnly> && !canPlayNight<Civilian> && !canPlayNight<RulesWithoutAct>);
static_assert(canPlayNight<Commissar> && canPlayNight<Ninja> && canPlayNight<HumanPlayer>);
static_assert(!canDriveNight<Civilian> && !canDriveNight<NoRules> && !canDriveNight<WrongRuleType> &&
              !canDriveNight<HumanPlayer>);
static_assert(canDriveNight<Doctor> && canDriveNight<Journalist> && canDriveNight<Bodyguard>);
static_assert(!canMakeBot<NoVote> && !canMakeBot<Anonymous> && !canMakeBot<NeedsTeam> && !canMakeBot<NotAPlayer>);
static_assert(canMakeBot<Civilian> && canMakeBot<Ninja> && canMakeBot<Journalist> && canMakeBot<Bodyguard>);

// ------------------------------------------------------------ помощники

// Снимок партии из n живых игроков без событий.
SharedPtr<const GameView> freshView(int players) {
    auto view = MakeShared<GameView>();
    view->day = 1;
    for (int id = 1; id <= players; ++id) {
        view->players.push_back(PlayerPublicInfo{.id = id});
    }
    return view;
}

Request request(RequestType type, std::vector<int> targets) {
    return Request{.type = type, .view = freshView(6), .targets = std::move(targets)};
}

bool among(const std::vector<int>& items, int value) {
    return contains(items, value);
}

// Игроки 1..6: мафия, ниндзя, комиссар, доктор, журналист, мирный.
NightContext sixPlayers() {
    return NightContext{.alive = {1, 2, 3, 4, 5, 6},
                        .roles = {Role::Mafia, Role::Ninja, Role::Commissar, Role::Doctor, Role::Journalist,
                                  Role::Civilian},
                        .lastHeal = std::nullopt};
}

}  // namespace

// ------------------------------------------------------- ход через playTurn

TEST(CivilianSleepsAtNight) {
    auto civilian = makeBot<Civilian>(6, 1);
    const Response night = playTurn(*civilian, request(RequestType::Night, {1, 2}));
    CHECK(!night.valid);
    CHECK(night.error.find("нет ночного хода") != std::string::npos);

    const Response ballot = playTurn(*civilian, request(RequestType::DayVote, {1, 2, 3}));
    CHECK(ballot.valid && ballot.action == ActionType::Vote && among({1, 2, 3}, ballot.target));
}

TEST(NightRolesMoveThroughPlayTurn) {
    for (std::uint64_t seed = 0; seed < 50; ++seed) {
        auto commissar = makeBot<Commissar>(3, seed);
        const Response check = playTurn(*commissar, request(RequestType::Night, {1, 2, 4}));
        CHECK(Commissar::isValidNightMove(check, request(RequestType::Night, {1, 2, 4})));

        auto journalist = makeBot<Journalist>(5, seed);
        const Response compare = playTurn(*journalist, request(RequestType::Night, {1, 2, 3, 4}));
        CHECK(compare.action == ActionType::Compare);
        CHECK(among({1, 2, 3, 4}, compare.target) && among({1, 2, 3, 4}, compare.secondTarget));
        CHECK(compare.target != compare.secondTarget);

        auto bodyguard = makeBot<Bodyguard>(4, seed);
        const Response guard = playNightTurn(*bodyguard, request(RequestType::Night, {1, 2, 3}));
        CHECK(guard.action == ActionType::Guard && among({1, 2, 3}, guard.target));

        auto ninja = makeBot<Ninja>(2, seed);
        ninja->setTeam({1, 2});
        const Response kill = playTurn(*ninja, request(RequestType::Night, {3, 4, 5}));
        CHECK(kill.action == ActionType::Kill && among({3, 4, 5}, kill.target));
        const Response ballot = playTurn(*ninja, request(RequestType::DayVote, {1, 3, 4}));
        CHECK(ballot.action == ActionType::Vote && ballot.target != 1);  // за напарника не голосует
    }
}

// ---------------------------------------------------- правила ночного хода

TEST(NightTargetsFollowRoleRules) {
    NightContext context = sixPlayers();
    CHECK(Mafioso::nightTargets(context, 1) == (std::vector<int>{3, 4, 5, 6}));  // вне команды мафии
    CHECK(Ninja::nightTargets(context, 2) == (std::vector<int>{3, 4, 5, 6}));
    CHECK(Commissar::nightTargets(context, 3) == (std::vector<int>{1, 2, 4, 5, 6}));
    CHECK(Bodyguard::nightTargets(context, 6) == (std::vector<int>{1, 2, 3, 4, 5}));

    context.lastHeal = 4;
    CHECK(Doctor::nightTargets(context, 4) == (std::vector<int>{1, 2, 3, 5, 6}));  // себя можно, прошлую цель нельзя

    context.alive = {5, 6};
    CHECK(Journalist::nightTargets(context, 5).empty());  // сравнивать некого
}

TEST(NightMovesAreValidatedByRoleRules) {
    const Request night = request(RequestType::Night, {2, 3, 4});
    Response compare;
    compare.action = ActionType::Compare;
    compare.target = 2;
    compare.secondTarget = 2;
    CHECK(!Journalist::isValidNightMove(compare, night));  // нужны двое разных
    compare.secondTarget = 4;
    CHECK(Journalist::isValidNightMove(compare, night));

    Response kill;
    kill.action = ActionType::Kill;
    kill.target = 3;
    CHECK(Maniac::isValidNightMove(kill, night));
    CHECK(!Commissar::isValidNightMove(kill, night));  // комиссар проверяет или стреляет
    kill.target = 5;
    CHECK(!Maniac::isValidNightMove(kill, night));  // цель не из списка
}

// ------------------------------------------------ управление ночью у ведущего

TEST(VisitNightRoleFindsRoleClass) {
    for (Role role : kAllRoles) {
        Role visited = Role::Civilian;
        const bool found = visitNightRole(role, [&]<NightRole R>() { visited = R::kRole; });
        CHECK_EQ(found, hasNightAction(role));
        if (found) {
            CHECK(visited == role);
        }
    }
}

// Запасной ход ведущего за любую ночную роль проходит проверку той же роли.
TEST(FallbackMovesAreValidForEveryNightRole) {
    Rng rng(7);
    const Request night = request(RequestType::Night, {2, 3, 4});
    int checked = 0;
    for (Role role : kAllRoles) {
        visitNightRole(role, [&]<NightRole R>() {
            for (int i = 0; i < 20; ++i) {
                CHECK(R::isValidNightMove(R::fallbackNightMove(night, rng), night));
            }
            ++checked;
        });
    }
    CHECK_EQ(checked, 7);
}

TEST(MakeBotCreatesRoleClasses) {
    CHECK(makeBot<Ninja>(3, 1)->role() == Role::Ninja);
    CHECK(makeBot<Journalist>(4, 1)->role() == Role::Journalist);
    CHECK(makeBot<Bodyguard>(5, 1)->role() == Role::Bodyguard);
    SharedPtr<Player> asPlayer = makeBot<Civilian>(6, 1);  // конвертация Derived → Base
    CHECK_EQ(asPlayer->id(), 6);
}

MAFIA_TEST_MAIN()
