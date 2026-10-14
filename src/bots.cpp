#include "mafia/bots.hpp"

#include <algorithm>
#include <cstdio>
#include <ranges>
#include <utility>

#include "mafia/ranges_util.hpp"

#include "mafia/player_concepts.hpp"

namespace mafia {

namespace {

bool revealedMafia(const PublicEvent& event) {
    return (event.role && teamOf(*event.role) == Team::Mafia) || (event.side && *event.side == Side::Mafia);
}

}  // namespace

// ---------------------------------------------------------------- BotPlayer

BotPlayer::BotPlayer(int id, Role role, std::uint64_t seed) : Player(id, role), rng_(seed) {}

Response BotPlayer::discuss(const Request& request) {
    absorb(request.view);
    return chooseStatement(request);
}

Response BotPlayer::vote(const Request& request) {
    absorb(request.view);
    return chooseVote(request);
}

// Разбирает только новые события снимка — бот помнит, сколько уже обработал.
void BotPlayer::absorb(const SharedPtr<const GameView>& view) {
    view_ = view;
    const auto size = static_cast<std::size_t>(view_->playerCount() + 1);
    if (suspicion_.size() < size) {
        suspicion_.resize(size, 0.0);
    }
    const auto& events = view_->events;
    for (; processedEvents_ < events.size(); ++processedEvents_) {
        observe(events[processedEvents_]);
    }
}

void BotPlayer::observe(const PublicEvent& event) {
    switch (event.type) {
        case EventType::Statement:
            if (event.actor == id()) {
                break;
            }
            if (event.statement == ActionType::Accuse) {
                accusations_.push_back({event.day, event.actor, event.target, event.confident});
                if (event.target == id()) {
                    // Я знаю свою роль: кто обвиняет меня, тот ошибается или врёт.
                    addSuspicion(event.actor, event.confident ? 1.5 : 1.0);
                } else {
                    addSuspicion(event.target, (event.confident ? 0.6 : 0.3) * trust(event.actor));
                }
            } else if (event.statement == ActionType::ClaimInnocent) {
                addSuspicion(event.actor, -0.05);
            }
            break;
        case EventType::Vote:
            votes_.push_back({event.day, event.actor, event.target});
            if (event.target == id() && event.actor != id()) {
                addSuspicion(event.actor, 0.5);
            }
            break;
        case EventType::Death:
            observeDeath(event);
            break;
        case EventType::Abstain:
            break;
        case EventType::Saved:
            // Мафия своих не трогает: её жертва скорее мирная.
            if (contains(event.causes, DeathCause::Mafia)) {
                addSuspicion(event.target, -1.0);
            }
            break;
    }
}

// Главный источник информации — раскрытая роль (сторона) выбывшего.
void BotPlayer::observeDeath(const PublicEvent& event) {
    const int dead = event.target;
    const bool wasMafia = revealedMafia(event);
    const bool executed = contains(event.causes, DeathCause::Execution);

    if (executed) {
        for (const auto& vote : votes_) {
            if (vote.day == event.day && vote.target == dead) {
                addSuspicion(vote.voter, wasMafia ? -1.0 : 0.8);
            }
        }
    }
    for (const auto& accusation : accusations_) {
        if (accusation.target == dead) {
            addSuspicion(accusation.accuser, wasMafia ? -0.6 : 0.4);
        }
    }
    if (wasMafia) {
        // Кого мафиози обвинял — скорее мирные; кто голосовал с ним заодно — под подозрением.
        for (const auto& accusation : accusations_) {
            if (accusation.accuser == dead) {
                addSuspicion(accusation.target, -0.5);
            }
        }
        for (const auto& mafiaVote : votes_) {
            if (mafiaVote.voter != dead) {
                continue;
            }
            for (const auto& other : votes_) {
                if (other.day == mafiaVote.day && other.target == mafiaVote.target && other.voter != dead) {
                    addSuspicion(other.voter, 0.3);
                }
            }
        }
    } else if (!executed) {
        // Мирного убили ночью — возможно, он был прав в своих обвинениях.
        for (const auto& accusation : accusations_) {
            if (accusation.accuser == dead) {
                addSuspicion(accusation.target, 0.6);
            }
        }
    }
}

Response BotPlayer::chooseStatement(const Request& request) {
    const int suspect = mostSuspicious(request.targets, 0.5);
    if (suspect != 0) {
        const double level = suspicion(suspect);
        if (level >= 0.6) {
            return action(ActionType::Accuse, suspect, "подозрение " + format(level), level >= 2.5);
        }
        if (rng_.chance(0.2)) {
            return action(ActionType::Accuse, suspect, "интуиция, явных улик нет");
        }
    }
    return action(ActionType::ClaimInnocent, 0, "явных подозреваемых нет");
}

Response BotPlayer::chooseVote(const Request& request) {
    const int suspect = mostSuspicious(request.targets, 0.7);
    return action(ActionType::Vote, suspect, "подозрение " + format(suspicion(suspect)));
}

int BotPlayer::mostSuspicious(const std::vector<int>& candidates, double noise) {
    return pickBest(candidates, noise, [this](int player) { return suspicion(player); });
}

double BotPlayer::suspicion(int player) const {
    const auto index = static_cast<std::size_t>(player);
    return index < suspicion_.size() ? suspicion_[index] : 0.0;
}

void BotPlayer::addSuspicion(int player, double delta) {
    if (player <= 0 || player == id()) {
        return;
    }
    const auto index = static_cast<std::size_t>(player);
    if (index >= suspicion_.size()) {
        suspicion_.resize(index + 1, 0.0);
    }
    suspicion_[index] += delta;
}

// Словам подозрительных игроков бот верит меньше.
double BotPlayer::trust(int player) const {
    return std::clamp(1.0 - suspicion(player) / 3.0, 0.2, 1.5);
}

// Активные обвинители — вероятные ночные жертвы.
double BotPlayer::activity(int player) const {
    return weightOfAccusations([player](const AccusationRecord& a) { return a.accuser == player; });
}

Response BotPlayer::action(ActionType type, int target, std::string reason, bool confident) {
    Response response;
    response.action = type;
    response.target = target;
    response.confident = confident;
    response.reason = std::move(reason);
    return response;
}

std::string BotPlayer::format(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return buffer;
}

// ----------------------------------------------------------------- Civilian

Civilian::Civilian(int id, std::uint64_t seed) : BotPlayer(id, kRole, seed) {}

// Мирный житель — DayPlayer, но не NightPlayer: выбирается дневная перегрузка playTurn.
Response Civilian::handle(const Request& request) {
    return playTurn(*this, request);
}

// ----------------------------------------------------------------- NightBot

Response NightBot::act(const Request& request) {
    absorb(request.view);
    return chooseNightAction(request);
}

bool NightBot::isTarget(const Request& request, int player) {
    return contains(request.targets, player);
}

Response NightBot::randomMove(ActionType type, const Request& request, Rng& rng) {
    Response response;
    if (request.targets.empty()) {
        return response;
    }
    response.action = type;
    response.target = rng.pick(request.targets);
    return response;
}

// ------------------------------------------------------------- Mafioso, Ninja

Mafioso::Mafioso(int id, std::uint64_t seed) : Mafioso(id, kRole, seed) {}

Mafioso::Mafioso(int id, Role role, std::uint64_t seed) : NightBot(id, role, seed) {}

Ninja::Ninja(int id, std::uint64_t seed) : Mafioso(id, kRole, seed) {}

Response Mafioso::handle(const Request& request) {
    return playTurn(*this, request);
}

Response Ninja::handle(const Request& request) {
    return playTurn(*this, request);
}

std::vector<int> Mafioso::nightTargets(const NightContext& context, int) {
    return context.aliveOutsideTeam(Team::Mafia);
}

bool Mafioso::isValidNightMove(const Response& response, const Request& request) {
    return response.action == ActionType::Kill && isTarget(request, response.target);
}

Response Mafioso::fallbackNightMove(const Request& request, Rng& rng) {
    return randomMove(ActionType::Kill, request, rng);
}

// Предложение мафиози; ведущий передаёт ходы по возрастанию номеров, поэтому первым идёт босс.
void Mafioso::recordNightMove(NightMoves& moves, int self, const Response& response) {
    moves.mafia.push_back({self, response.target});
}

void Mafioso::setTeam(std::vector<int> team) {
    team_ = std::move(team);
}

bool Mafioso::isTeammate(int player) const {
    return contains(team_, player);
}

double Mafioso::threat(int player) const {
    const auto index = static_cast<std::size_t>(player);
    return index < threat_.size() ? threat_[index] : 0.0;
}

// Насколько город сегодня давит на игрока: удобная жертва для голосования.
double Mafioso::pressureToday(int player) const {
    const int today = view().day;
    return weightOfAccusations(
        [today, player](const AccusationRecord& a) { return a.day == today && a.target == player; });
}

// Кандидаты вне команды мафии (если таких нет — все).
std::vector<int> Mafioso::outsiders(const std::vector<int>& candidates) const {
    std::vector<int> result =
        toVector(candidates | std::views::filter([this](int candidate) { return !isTeammate(candidate); }));
    return result.empty() ? candidates : result;
}

void Mafioso::observe(const PublicEvent& event) {
    BotPlayer::observe(event);
    const auto size = static_cast<std::size_t>(view().playerCount() + 1);
    if (threat_.size() < size) {
        threat_.resize(size, 0.0);
    }
    if (isTeammate(event.actor) || event.actor <= 0 || !isTeammate(event.target)) {
        return;
    }
    const auto actor = static_cast<std::size_t>(event.actor);
    if (event.type == EventType::Statement && event.statement == ActionType::Accuse) {
        threat_[actor] += accusationWeight(event.confident);
    } else if (event.type == EventType::Vote) {
        threat_[actor] += 0.7;
    }
}

Response Mafioso::chooseStatement(const Request& request) {
    const std::vector<int> candidates = outsiders(request.targets);
    const int dangerous = pickBest(candidates, 0.3, [this](int p) { return threat(p); });
    if (threat(dangerous) >= 1.0 && rng().chance(0.7)) {
        return action(ActionType::Accuse, dangerous, "переводит стрелки на того, кто давит на мафию",
                      rng().chance(bluffChance()));
    }
    const int pressured = pickBest(candidates, 0.3, [this](int p) { return pressureToday(p); });
    if (pressureToday(pressured) >= 1.0 && rng().chance(0.6)) {
        return action(ActionType::Accuse, pressured, "поддерживает обвинение города против мирного");
    }
    if (rng().chance(0.3)) {
        return action(ActionType::Accuse, rng().pick(candidates), "сеет сомнения");
    }
    return action(ActionType::ClaimInnocent, 0, "маскируется под мирного");
}

Response Mafioso::chooseVote(const Request& request) {
    const std::vector<int> candidates = outsiders(request.targets);
    const int target = pickBest(candidates, 0.6, [this](int p) { return pressureToday(p) + 0.6 * threat(p); });
    return action(ActionType::Vote, target,
                  "давление города " + format(pressureToday(target)) + ", угроза " + format(threat(target)));
}

Response Mafioso::chooseNightAction(const Request& request) {
    const int target = pickBest(request.targets, 1.0, [this](int p) { return threat(p); });
    return action(ActionType::Kill, target, "угроза для мафии " + format(threat(target)));
}

// ---------------------------------------------------------------- Commissar

Commissar::Commissar(int id, std::uint64_t seed) : NightBot(id, kRole, seed) {}

Response Commissar::handle(const Request& request) {
    return playTurn(*this, request);
}

std::vector<int> Commissar::nightTargets(const NightContext& context, int self) {
    return context.aliveExcept(self);
}

bool Commissar::isValidNightMove(const Response& response, const Request& request) {
    return (response.action == ActionType::Check || response.action == ActionType::Shoot) &&
           isTarget(request, response.target);
}

Response Commissar::fallbackNightMove(const Request& request, Rng& rng) {
    return randomMove(ActionType::Check, request, rng);
}

void Commissar::recordNightMove(NightMoves& moves, int self, const Response& response) {
    moves.commissar = NightMoves::Move{self, response.target};
    moves.commissarShoots = response.action == ActionType::Shoot;
}

void Commissar::onInfo(const Request& request) {
    if (!request.check) {
        return;
    }
    checked_[request.check->target] = request.check->isMafia;
    addSuspicion(request.check->target, request.check->isMafia ? 100.0 : -100.0);
}

int Commissar::knownMafia(const std::vector<int>& candidates) const {
    for (int candidate : candidates) {
        const auto it = checked_.find(candidate);
        if (it != checked_.end() && it->second) {
            return candidate;
        }
    }
    return 0;
}

Response Commissar::chooseStatement(const Request& request) {
    if (const int mafia = knownMafia(request.targets)) {
        return action(ActionType::Accuse, mafia, "проверка показала мафию", true);
    }
    return BotPlayer::chooseStatement(request);
}

Response Commissar::chooseVote(const Request& request) {
    if (const int mafia = knownMafia(request.targets)) {
        return action(ActionType::Vote, mafia, "проверка показала мафию");
    }
    return BotPlayer::chooseVote(request);
}

Response Commissar::chooseNightAction(const Request& request) {
    if (const int mafia = knownMafia(request.targets)) {
        return action(ActionType::Shoot, mafia, "стреляет в проверенного мафиози");
    }
    const std::vector<int> unchecked = toVector(
        request.targets | std::views::filter([this](int candidate) { return !checked_.contains(candidate); }));
    const int target = mostSuspicious(unchecked.empty() ? request.targets : unchecked, 0.8);
    return action(ActionType::Check, target, "подозрение " + format(suspicion(target)));
}

// ------------------------------------------------------------------- Doctor

Doctor::Doctor(int id, std::uint64_t seed) : NightBot(id, kRole, seed) {}

Response Doctor::handle(const Request& request) {
    return playTurn(*this, request);
}

std::vector<int> Doctor::nightTargets(const NightContext& context, int) {
    // Не два раза подряд одного и того же.
    return context.lastHeal ? allExcept(context.alive, *context.lastHeal) : context.alive;
}

bool Doctor::isValidNightMove(const Response& response, const Request& request) {
    return response.action == ActionType::Heal && isTarget(request, response.target);
}

Response Doctor::fallbackNightMove(const Request& request, Rng& rng) {
    return randomMove(ActionType::Heal, request, rng);
}

void Doctor::recordNightMove(NightMoves& moves, int self, const Response& response) {
    moves.doctor = NightMoves::Move{self, response.target};
}

Response Doctor::chooseNightAction(const Request& request) {
    const int target = pickBest(request.targets, 1.0, [this](int p) {
        double value = 0.3 * activity(p) - 0.5 * std::max(0.0, suspicion(p));
        if (p == id()) {
            value += 0.8;
        }
        return value;
    });
    return action(ActionType::Heal, target,
                  target == id() ? "лечит себя" : "активность " + format(activity(target)));
}

// ------------------------------------------------------------------- Maniac

Maniac::Maniac(int id, std::uint64_t seed) : NightBot(id, kRole, seed) {}

Response Maniac::handle(const Request& request) {
    return playTurn(*this, request);
}

std::vector<int> Maniac::nightTargets(const NightContext& context, int self) {
    return context.aliveExcept(self);
}

bool Maniac::isValidNightMove(const Response& response, const Request& request) {
    return response.action == ActionType::Kill && isTarget(request, response.target);
}

Response Maniac::fallbackNightMove(const Request& request, Rng& rng) {
    return randomMove(ActionType::Kill, request, rng);
}

void Maniac::recordNightMove(NightMoves& moves, int self, const Response& response) {
    moves.maniac = NightMoves::Move{self, response.target};
}

Response Maniac::chooseNightAction(const Request& request) {
    const int target = mostSuspicious(request.targets, 1.5);
    return action(ActionType::Kill, target, "подозрение " + format(suspicion(target)));
}

// --------------------------------------------------------------- Journalist

Journalist::Journalist(int id, std::uint64_t seed) : NightBot(id, kRole, seed) {}

Response Journalist::handle(const Request& request) {
    return playTurn(*this, request);
}

std::vector<int> Journalist::nightTargets(const NightContext& context, int self) {
    std::vector<int> targets = context.aliveExcept(self);
    if (targets.size() < 2) {
        targets.clear();  // сравнивать некого — этой ночью хода нет
    }
    return targets;
}

bool Journalist::isValidNightMove(const Response& response, const Request& request) {
    return response.action == ActionType::Compare && isTarget(request, response.target) &&
           isTarget(request, response.secondTarget) && response.target != response.secondTarget;
}

Response Journalist::fallbackNightMove(const Request& request, Rng& rng) {
    Response response = randomMove(ActionType::Compare, request, rng);
    if (response.action == ActionType::Compare) {
        const std::vector<int> others = allExcept(request.targets, response.target);
        response.secondTarget = others.empty() ? response.target : rng.pick(others);
    }
    return response;
}

void Journalist::recordNightMove(NightMoves& moves, int self, const Response& response) {
    moves.journalist = NightMoves::Comparison{self, response.target, response.secondTarget};
}

// Сравнивает подозрительного с надёжным (с долей случайности): «разный статус»
// скорее всего выдаёт мафию в первом.
Response Journalist::chooseNightAction(const Request& request) {
    const int suspect = mostSuspicious(request.targets, 0.8);
    const std::vector<int> others = allExcept(request.targets, suspect);
    const int reliable = pickBest(others, 0.8, [this](int p) { return -suspicion(p); });
    Response response = action(ActionType::Compare, suspect,
                                "подозрения " + format(suspicion(suspect)) + " и " + format(suspicion(reliable)));
    response.secondTarget = reliable;
    return response;
}

void Journalist::onInfo(const Request& request) {
    if (!request.comparison) {
        return;
    }
    const CompareResult& result = *request.comparison;
    comparisons_.push_back(result);
    if (result.same) {
        // Мирных больше, поэтому «одинаковый статус» — скорее двое мирных.
        addSuspicion(result.first, -0.4);
        addSuspicion(result.second, -0.4);
        return;
    }
    const bool firstWorse = suspicion(result.first) >= suspicion(result.second);
    addSuspicion(firstWorse ? result.first : result.second, 1.5);
    addSuspicion(firstWorse ? result.second : result.first, -0.5);
}

// Когда сторона одного из сравненных раскрыта, сторона второго становится известной.
void Journalist::observe(const PublicEvent& event) {
    BotPlayer::observe(event);
    if (event.type != EventType::Death || (!event.role && !event.side)) {
        return;
    }
    const double deadMafia = deadLooksMafia(event);
    for (const CompareResult& result : comparisons_) {
        if (result.first != event.target && result.second != event.target) {
            continue;
        }
        const int partner = result.first == event.target ? result.second : result.first;
        // «Одинаковый статус» — партнёр выглядел так же, как погибший; «разный» — наоборот.
        const double partnerMafia = result.same ? deadMafia : 1.0 - deadMafia;
        addSuspicion(partner, 3.0 * partnerMafia - 1.0 * (1.0 - partnerMafia));
    }
}

// Вероятность того, что погибший при сравнении выглядел мафией. Сравнение, как и
// проверка комиссара, видит ниндзя мирным.
double Journalist::deadLooksMafia(const PublicEvent& death) const {
    if (death.role) {
        return checkSide(*death.role) == Side::Mafia ? 1.0 : 0.0;  // открытые объявления: роль известна
    }
    if (*death.side == Side::Town) {
        return 0.0;  // «мирный» — мирные роли и маньяк, все они выглядят мирными
    }
    // Закрытые объявления называют ниндзя «мафией»; кто именно погиб, неизвестно,
    // поэтому учитываем долю ниндзя в публично объявленном составе мафии.
    const int mafia = view().mafiaTotal;
    return mafia > 0 ? static_cast<double>(mafia - view().ninjaTotal) / mafia : 1.0;
}

// ---------------------------------------------------------------- Bodyguard

Bodyguard::Bodyguard(int id, std::uint64_t seed) : NightBot(id, kRole, seed) {}

Response Bodyguard::handle(const Request& request) {
    return playTurn(*this, request);
}

std::vector<int> Bodyguard::nightTargets(const NightContext& context, int self) {
    return context.aliveExcept(self);
}

bool Bodyguard::isValidNightMove(const Response& response, const Request& request) {
    return response.action == ActionType::Guard && isTarget(request, response.target);
}

Response Bodyguard::fallbackNightMove(const Request& request, Rng& rng) {
    return randomMove(ActionType::Guard, request, rng);
}

void Bodyguard::recordNightMove(NightMoves& moves, int self, const Response& response) {
    moves.bodyguard = NightMoves::Move{self, response.target};
}

// Прикрывает активных обвинителей — их мафия убивает первыми.
Response Bodyguard::chooseNightAction(const Request& request) {
    const int target = pickBest(request.targets, 1.0, [this](int p) {
        return 0.3 * activity(p) - 0.5 * std::max(0.0, suspicion(p));
    });
    return action(ActionType::Guard, target, "активность " + format(activity(target)));
}

}  // namespace mafia
