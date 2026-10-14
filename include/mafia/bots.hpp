#pragma once

// Программные игроки.
//
//   Player
//   └─ BotPlayer            день: discuss(), vote(); модель подозрений
//      ├─ Civilian          ночью спит — НЕ удовлетворяет NightPlayer
//      └─ NightBot          + ночной ход act()
//         ├─ Mafioso ─ Ninja
//         ├─ Commissar, Doctor, Maniac
//         └─ Journalist, Bodyguard
//
// Каждый конечный класс реализует handle() вызовом playTurn(*this, ...): нужную
// перегрузку компилятор выбирает по концептам (player_concepts.hpp) для точного
// типа роли. Классы ночных ролей дают и статические правила хода (концепт NightRole),
// по которым ведущий раздаёт цели и проверяет ответы.
//
// Публичные discuss/vote/act в игре вызывает только нить игрока через handle();
// тесты вызывают их напрямую, без нитей.

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <map>
#include <ranges>
#include <string>
#include <vector>

#include "mafia/night.hpp"
#include "mafia/player.hpp"
#include "mafia/random.hpp"

namespace mafia {

class BotPlayer : public Player {
public:
    BotPlayer(int id, Role role, std::uint64_t seed);

    // Каждый метод сначала учитывает новые события снимка, затем принимает решение.
    Response discuss(const Request& request);
    Response vote(const Request& request);

protected:
    struct AccusationRecord {
        int day;
        int accuser;
        int target;
        bool confident;
    };

    struct VoteRecord {
        int day;
        int voter;
        int target;
    };

    // Вес обвинения: уверенное («я точно знаю») весит вдвое больше обычного.
    static constexpr double accusationWeight(bool confident) noexcept { return confident ? 2.0 : 1.0; }

    // Суммарный вес обвинений, подходящих под условие (один проход по истории).
    template <std::predicate<const AccusationRecord&> Matches>
    double weightOfAccusations(Matches matches) const {
        double total = 0.0;
        std::ranges::for_each(
            accusations_ | std::views::filter(matches),
            [&total](bool confident) { total += accusationWeight(confident); }, &AccusationRecord::confident);
        return total;
    }

    // Решения роли; вызываются после учёта событий.
    virtual Response chooseStatement(const Request& request);
    virtual Response chooseVote(const Request& request);
    virtual void observe(const PublicEvent& event);

    void absorb(const SharedPtr<const GameView>& view);

    // Кандидат с наибольшей оценкой; случайный шум делает ботов менее предсказуемыми.
    template <class Score>
    int pickBest(const std::vector<int>& candidates, double noise, Score score) {
        int best = 0;
        double bestValue = 0.0;
        for (int candidate : candidates) {
            const double value = score(candidate) + rng_.uniform(0.0, noise);
            if (best == 0 || value > bestValue) {
                best = candidate;
                bestValue = value;
            }
        }
        return best;
    }

    int mostSuspicious(const std::vector<int>& candidates, double noise);
    double suspicion(int player) const;
    void addSuspicion(int player, double delta);
    double trust(int player) const;
    double activity(int player) const;  // насколько игрок активно обвиняет других

    const GameView& view() const { return *view_; }
    Rng& rng() { return rng_; }
    const std::vector<AccusationRecord>& accusations() const noexcept { return accusations_; }
    const std::vector<VoteRecord>& votes() const noexcept { return votes_; }

    static Response action(ActionType type, int target, std::string reason, bool confident = false);
    static std::string format(double value);

private:
    void observeDeath(const PublicEvent& event);

    Rng rng_;
    SharedPtr<const GameView> view_;
    std::size_t processedEvents_ = 0;
    std::vector<double> suspicion_;  // индекс = id игрока
    std::vector<AccusationRecord> accusations_;
    std::vector<VoteRecord> votes_;
};

class Civilian final : public BotPlayer {
public:
    static constexpr Role kRole = Role::Civilian;
    Civilian(int id, std::uint64_t seed);

protected:
    Response handle(const Request& request) override;
};

// Бот с ночным ходом.
class NightBot : public BotPlayer {
public:
    using BotPlayer::BotPlayer;

    Response act(const Request& request);

protected:
    virtual Response chooseNightAction(const Request& request) = 0;

    // Помощники для статических правил хода в наследниках.
    static bool isTarget(const Request& request, int player);
    static Response randomMove(ActionType type, const Request& request, Rng& rng);
};

// Мафиози знает напарников и оценивает «угрозу» — кто давит на мафию.
class Mafioso : public NightBot {
public:
    static constexpr Role kRole = Role::Mafia;
    Mafioso(int id, std::uint64_t seed);

    void setTeam(std::vector<int> team);  // вся команда мафии, включая ниндзя; до start()

    // Правила хода: убить игрока вне команды мафии.
    static std::vector<int> nightTargets(const NightContext& context, int self);
    static bool isValidNightMove(const Response& response, const Request& request);
    static Response fallbackNightMove(const Request& request, Rng& rng);
    static void recordNightMove(NightMoves& moves, int self, const Response& response);

protected:
    Mafioso(int id, Role role, std::uint64_t seed);

    Response handle(const Request& request) override;
    Response chooseStatement(const Request& request) override;
    Response chooseVote(const Request& request) override;
    Response chooseNightAction(const Request& request) override;
    void observe(const PublicEvent& event) override;

    // Вероятность уверенного («я точно знаю») ложного обвинения.
    virtual double bluffChance() const { return 0.2; }

private:
    bool isTeammate(int player) const;
    double threat(int player) const;
    double pressureToday(int player) const;
    std::vector<int> outsiders(const std::vector<int>& candidates) const;

    std::vector<int> team_;
    std::vector<double> threat_;
};

// Ниндзя — мафиози, невидимый для проверки комиссара; поэтому блефует смелее.
// Правила ночного хода те же, что у мафиози (наследуются).
class Ninja final : public Mafioso {
public:
    static constexpr Role kRole = Role::Ninja;
    Ninja(int id, std::uint64_t seed);

protected:
    Response handle(const Request& request) override;
    double bluffChance() const override { return 0.5; }
};

// Комиссар каждую ночь либо проверяет игрока, либо стреляет.
class Commissar final : public NightBot {
public:
    static constexpr Role kRole = Role::Commissar;
    Commissar(int id, std::uint64_t seed);

    // Правила хода: проверить или застрелить любого живого, кроме себя.
    static std::vector<int> nightTargets(const NightContext& context, int self);
    static bool isValidNightMove(const Response& response, const Request& request);
    static Response fallbackNightMove(const Request& request, Rng& rng);
    static void recordNightMove(NightMoves& moves, int self, const Response& response);

protected:
    Response handle(const Request& request) override;
    Response chooseStatement(const Request& request) override;
    Response chooseVote(const Request& request) override;
    Response chooseNightAction(const Request& request) override;
    void onInfo(const Request& request) override;

private:
    int knownMafia(const std::vector<int>& candidates) const;

    std::map<int, bool> checked_;  // id → мафия ли
};

class Doctor final : public NightBot {
public:
    static constexpr Role kRole = Role::Doctor;
    Doctor(int id, std::uint64_t seed);

    // Правила хода: вылечить любого живого (и себя), кроме прошлой цели.
    static std::vector<int> nightTargets(const NightContext& context, int self);
    static bool isValidNightMove(const Response& response, const Request& request);
    static Response fallbackNightMove(const Request& request, Rng& rng);
    static void recordNightMove(NightMoves& moves, int self, const Response& response);

protected:
    Response handle(const Request& request) override;
    Response chooseNightAction(const Request& request) override;
};

class Maniac final : public NightBot {
public:
    static constexpr Role kRole = Role::Maniac;
    Maniac(int id, std::uint64_t seed);

    // Правила хода: убить любого живого, кроме себя.
    static std::vector<int> nightTargets(const NightContext& context, int self);
    static bool isValidNightMove(const Response& response, const Request& request);
    static Response fallbackNightMove(const Request& request, Rng& rng);
    static void recordNightMove(NightMoves& moves, int self, const Response& response);

protected:
    Response handle(const Request& request) override;
    Response chooseNightAction(const Request& request) override;
};

// Журналист сравнивает двух игроков: одинаково ли они выглядят при проверке.
class Journalist final : public NightBot {
public:
    static constexpr Role kRole = Role::Journalist;
    Journalist(int id, std::uint64_t seed);

    // Правила хода: сравнить двух разных живых, кроме себя (если их меньше двух — хода нет).
    static std::vector<int> nightTargets(const NightContext& context, int self);
    static bool isValidNightMove(const Response& response, const Request& request);
    static Response fallbackNightMove(const Request& request, Rng& rng);
    static void recordNightMove(NightMoves& moves, int self, const Response& response);

protected:
    Response handle(const Request& request) override;
    Response chooseNightAction(const Request& request) override;
    void onInfo(const Request& request) override;
    void observe(const PublicEvent& event) override;

private:
    double deadLooksMafia(const PublicEvent& death) const;

    std::vector<CompareResult> comparisons_;
};

// Телохранитель прикрывает одного игрока и отвечает нападавшему.
class Bodyguard final : public NightBot {
public:
    static constexpr Role kRole = Role::Bodyguard;
    Bodyguard(int id, std::uint64_t seed);

    // Правила хода: прикрыть любого живого, кроме себя.
    static std::vector<int> nightTargets(const NightContext& context, int self);
    static bool isValidNightMove(const Response& response, const Request& request);
    static Response fallbackNightMove(const Request& request, Rng& rng);
    static void recordNightMove(NightMoves& moves, int self, const Response& response);

protected:
    Response handle(const Request& request) override;
    Response chooseNightAction(const Request& request) override;
};

}  // namespace mafia
