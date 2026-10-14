// Тесты разрешения ночи (resolveNight) — без нитей и ведущего.

#include <vector>

#include "mafia/night.hpp"
#include "test_framework.hpp"

using mafia::DeathCause;
using mafia::NightMoves;
using mafia::resolveNight;
using mafia::Role;

namespace {

// Игроки 1..9: две мафии, ниндзя, комиссар, доктор, маньяк, журналист, телохранитель, мирный.
const std::vector<Role> kRoles = {Role::Mafia,  Role::Mafia,      Role::Ninja,     Role::Commissar, Role::Doctor,
                                  Role::Maniac, Role::Journalist, Role::Bodyguard, Role::Civilian};

constexpr int kBoss = 1;
constexpr int kMafia2 = 2;
constexpr int kNinja = 3;
constexpr int kCommissar = 4;
constexpr int kDoctor = 5;
constexpr int kManiac = 6;
constexpr int kJournalist = 7;
constexpr int kBodyguard = 8;
constexpr int kCivilian = 9;

using Causes = std::vector<DeathCause>;

}  // namespace

TEST(NoMovesNoDeaths) {
    const auto outcome = resolveNight(NightMoves{}, kRoles);
    CHECK(!outcome.mafiaVictim);
    CHECK(outcome.attacks.empty());
    CHECK(outcome.deaths.empty());
    CHECK(!outcome.check && !outcome.comparison && !outcome.counterKilled);
}

TEST(MafiaMajorityWins) {
    NightMoves moves;
    moves.mafia = {{kBoss, kCivilian}, {kMafia2, kJournalist}, {kNinja, kJournalist}};
    const auto outcome = resolveNight(moves, kRoles);
    CHECK(outcome.mafiaVictim == kJournalist);
    CHECK_EQ(outcome.mafiaExecutor, kMafia2);  // первый из голосовавших за жертву
    CHECK(outcome.deaths == std::vector<int>{kJournalist});
}

TEST(MafiaTieBossDecides) {
    NightMoves moves;
    moves.mafia = {{kBoss, kCivilian}, {kMafia2, kJournalist}};
    const auto outcome = resolveNight(moves, kRoles);
    CHECK(outcome.mafiaVictim == kCivilian);
    CHECK_EQ(outcome.mafiaExecutor, kBoss);
}

TEST(CauseOrderMafiaFirstThenByShooter) {
    NightMoves moves;
    moves.mafia = {{kBoss, kCivilian}};
    moves.maniac = NightMoves::Move{kManiac, kCivilian};
    moves.commissar = NightMoves::Move{kCommissar, kCivilian};
    moves.commissarShoots = true;
    const auto outcome = resolveNight(moves, kRoles);
    CHECK(outcome.attacks.at(kCivilian) == (Causes{DeathCause::Mafia, DeathCause::Commissar, DeathCause::Maniac}));
    CHECK(outcome.deaths == std::vector<int>{kCivilian});
}

TEST(DoctorSavesFromEveryShot) {
    NightMoves moves;
    moves.mafia = {{kBoss, kCivilian}};
    moves.maniac = NightMoves::Move{kManiac, kCivilian};
    moves.doctor = NightMoves::Move{kDoctor, kCivilian};
    const auto outcome = resolveNight(moves, kRoles);
    CHECK(outcome.deaths.empty());
    CHECK(outcome.healed.at(kCivilian) == (Causes{DeathCause::Mafia, DeathCause::Maniac}));
}

TEST(DeathsAreSortedAndHealOnlyHelpsTarget) {
    NightMoves moves;
    moves.mafia = {{kBoss, kJournalist}};
    moves.maniac = NightMoves::Move{kManiac, kCommissar};
    moves.doctor = NightMoves::Move{kDoctor, kDoctor};
    const auto outcome = resolveNight(moves, kRoles);
    CHECK(outcome.deaths == (std::vector<int>{kCommissar, kJournalist}));
    CHECK(outcome.healed.empty());
}

TEST(BodyguardSavesAndShootsMafiaExecutor) {
    NightMoves moves;
    moves.mafia = {{kBoss, kCivilian}, {kMafia2, kCivilian}};
    moves.bodyguard = NightMoves::Move{kBodyguard, kCivilian};
    const auto outcome = resolveNight(moves, kRoles);
    CHECK(outcome.guarded.at(kCivilian) == Causes{DeathCause::Mafia});
    CHECK(outcome.counterKilled == kBoss);
    CHECK(outcome.attacks.at(kBoss) == Causes{DeathCause::Bodyguard});
    CHECK(outcome.deaths == std::vector<int>{kBoss});
}

TEST(BodyguardShootsLowestNumberedAttacker) {
    NightMoves moves;
    moves.mafia = {{kNinja, kCivilian}};  // в живых остался только ниндзя
    moves.maniac = NightMoves::Move{kManiac, kCivilian};
    moves.bodyguard = NightMoves::Move{kBodyguard, kCivilian};
    const auto outcome = resolveNight(moves, kRoles);
    CHECK(outcome.counterKilled == kNinja);
    CHECK(outcome.deaths == std::vector<int>{kNinja});

    NightMoves maniacOnly;
    maniacOnly.maniac = NightMoves::Move{kManiac, kCivilian};
    maniacOnly.bodyguard = NightMoves::Move{kBodyguard, kCivilian};
    const auto second = resolveNight(maniacOnly, kRoles);
    CHECK(second.counterKilled == kManiac);
    CHECK(second.deaths == std::vector<int>{kManiac});
}

TEST(BodyguardOnlyShieldsFromCommissar) {
    NightMoves moves;
    moves.commissar = NightMoves::Move{kCommissar, kCivilian};
    moves.commissarShoots = true;
    moves.bodyguard = NightMoves::Move{kBodyguard, kCivilian};
    const auto outcome = resolveNight(moves, kRoles);
    CHECK(outcome.deaths.empty());
    CHECK(!outcome.counterKilled);
    CHECK(outcome.guarded.at(kCivilian) == Causes{DeathCause::Commissar});
}

TEST(DoctorCanSaveAttackerFromBodyguard) {
    NightMoves moves;
    moves.mafia = {{kBoss, kCivilian}};
    moves.bodyguard = NightMoves::Move{kBodyguard, kCivilian};
    moves.doctor = NightMoves::Move{kDoctor, kBoss};
    const auto outcome = resolveNight(moves, kRoles);
    CHECK(outcome.counterKilled == kBoss);
    CHECK(outcome.healed.at(kBoss) == Causes{DeathCause::Bodyguard});
    CHECK(outcome.deaths.empty());
}

TEST(UnattackedWardChangesNothing) {
    NightMoves moves;
    moves.maniac = NightMoves::Move{kManiac, kJournalist};
    moves.bodyguard = NightMoves::Move{kBodyguard, kCivilian};
    const auto outcome = resolveNight(moves, kRoles);
    CHECK(!outcome.counterKilled);
    CHECK(outcome.guarded.empty());
    CHECK(outcome.deaths == std::vector<int>{kJournalist});
}

TEST(CommissarSeesNinjaAndManiacAsTown) {
    const auto check = [](int target) {
        NightMoves moves;
        moves.commissar = NightMoves::Move{kCommissar, target};
        const auto outcome = resolveNight(moves, kRoles);
        CHECK(outcome.attacks.empty());
        return outcome.check->isMafia;
    };
    CHECK(check(kBoss));
    CHECK(!check(kNinja));
    CHECK(!check(kManiac));
    CHECK(!check(kCivilian));
}

TEST(JournalistComparesApparentSides) {
    const auto same = [](int first, int second) {
        NightMoves moves;
        moves.journalist = NightMoves::Comparison{kJournalist, first, second};
        const auto outcome = resolveNight(moves, kRoles);
        CHECK_EQ(outcome.comparison->first, first);
        CHECK_EQ(outcome.comparison->second, second);
        return outcome.comparison->same;
    };
    CHECK(same(kBoss, kMafia2));
    CHECK(!same(kBoss, kCivilian));
    CHECK(!same(kBoss, kNinja));      // ниндзя выглядит мирным
    CHECK(same(kNinja, kManiac));     // оба выглядят мирными
    CHECK(same(kManiac, kCivilian));
}

MAFIA_TEST_MAIN()
