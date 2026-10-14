#include "mafia/moderator.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <thread>

#include "mafia/ai_agent.hpp"
#include "mafia/bots.hpp"
#include "mafia/file_log.hpp"
#include "mafia/human_player.hpp"
#include "mafia/night.hpp"
#include "mafia/player_concepts.hpp"
#include "mafia/player_runtime.hpp"
#include "mafia/ranges_util.hpp"
#include "mafia/role_dispatch.hpp"

namespace mafia {

namespace {

constexpr int kMaxDays = 100;

// Паузы, чтобы за партией было удобно следить (отключаются --no-pauses).
using namespace std::chrono_literals;
constexpr auto kLinePause = 500ms;    // между репликами и голосами
constexpr auto kNightPause = 2000ms;  // «ночь проходит» перед утренними объявлениями
constexpr auto kRoundPause = 1500ms;  // между днём и ночью

// ИИ-агенты: не больше 10 запросов к API за раунд на всю партию; агентов
// выбирает свой генератор (соль не пересекается с номерами игроков).
constexpr int kAiCallsPerRound = 10;
constexpr std::uint64_t kAgentSeedSalt = 1'000'003;

// Характеристика агента, если в YAML (ai_agents.characteristics) их нет: набор
// характеров живёт в configs/ai_agents.yaml, в коде — только нейтральная.
const std::vector<std::string>& defaultCharacteristics() {
    static const std::vector<std::string> characteristics = {"обычный игрок без особых привычек"};
    return characteristics;
}

// Человек закрыл ввод (Ctrl+D) — партию продолжить нельзя.
struct InputClosed {};

std::string plural(std::size_t n, const char* one, const char* few, const char* many) {
    const std::size_t lastTwo = n % 100;
    const std::size_t last = n % 10;
    if (lastTwo >= 11 && lastTwo <= 14) {
        return many;
    }
    if (last == 1) {
        return one;
    }
    if (last >= 2 && last <= 4) {
        return few;
    }
    return many;
}

std::string causeList(const std::vector<DeathCause>& causes) {
    std::string text;
    for (std::size_t i = 0; i < causes.size(); ++i) {
        text += (i == 0 ? "" : ", ") + std::string(causeName(causes[i]));
    }
    return text;
}

std::string roleHint(Role role) {
    switch (role) {
        case Role::Civilian:
            return "Ночью вы спите. Днём ищите мафию и голосуйте против неё.";
        case Role::Mafia:
            return "Ночью вместе с напарниками выбираете жертву. Днём притворяйтесь мирным.";
        case Role::Commissar:
            return "Каждую ночь либо проверяете игрока, либо стреляете в него.";
        case Role::Doctor:
            return "Каждую ночь лечите одного игрока; можно себя, но не одного и того же две ночи подряд.";
        case Role::Maniac:
            return "Играете сам за себя и каждую ночь убиваете. Победа — остаться один на один с мирным.";
        case Role::Journalist:
            return "Каждую ночь сравниваете двух игроков и узнаёте, одинаковый ли у них статус "
                   "(маньяк и ниндзя выглядят мирными).";
        case Role::Bodyguard:
            return "Каждую ночь прикрываете одного игрока (не себя): если в него стреляют мафия или маньяк, "
                   "он выживет, а один из нападавших погибнет.";
        case Role::Ninja:
            return "Вы мафиози, но комиссар при проверке видит вас мирным. Ночью вместе с напарниками "
                   "выбираете жертву.";
    }
    return {};
}

// Порядок, в котором ведущий будит ночные роли.
constexpr std::array<Role, 5> kNightOrder = {Role::Commissar, Role::Doctor, Role::Maniac, Role::Journalist,
                                             Role::Bodyguard};

}  // namespace

Moderator::Moderator(const GameConfig& config, RoleConfig roles, std::istream& humanInput, std::ostream& out,
                     SharedPtr<LlmClient> llm)
    : config_(config),
      roleConfig_(std::move(roles)),
      rng_(mixSeed(config.seed, 0)),
      humanInput_(humanInput),
      log_(MakeShared<Logger>(out, config.fullLog, config.interactive && config.fullLog)),
      state_(MakeShared<GameState>()),
      llm_(std::move(llm)) {}

Moderator::~Moderator() {
    shutdown();
}

Winner Moderator::run() {
    setup();
    announceStart();

    Winner winner = Winner::Draw;
    try {
        startDay();
        log_->announce("Первый день — знакомство, голосования нет.");
        discussion();
        for (;;) {
            night();
            if (const auto result = checkWinner()) {
                winner = *result;
                break;
            }
            discussion();
            voting();
            if (const auto result = checkWinner()) {
                winner = *result;
                break;
            }
            if (state_->day >= kMaxDays) {
                log_->announce("Игра затянулась — ведущий объявляет ничью.");
                break;
            }
        }
    } catch (const InputClosed&) {
        log_->announce("");
        log_->announce("Ввод закончился — партия прервана.");
        emit([&](GameEventSink& sink) { sink.gameEnded(Winner::Aborted, state_->day); });
        shutdown();
        return Winner::Aborted;
    }

    finish(winner);
    shutdown();
    return winner;
}

// ------------------------------------------------------------------ подготовка

void Moderator::setup() {
    const int count = config_.players;
    const RoleSetup roleSetup = buildRoles(roleConfig_, count, config_.mafiaDivisor);
    composition_ = roleSetup.roles;
    skippedRoles_ = roleSetup.skipped;
    std::vector<Role> roles = composition_;
    rng_.shuffle(roles);

    state_->roles = roles;
    state_->alive.assign(roles.size(), true);
    for (int id = 1; id <= count; ++id) {
        state_->publicInfo.push_back(PlayerPublicInfo{.id = id});
    }
    if (config_.interactive) {
        humanId_ = rng_.uniformInt(1, count);
    }
    chooseAgents();

    // Иерархия ролей: класс роли находит visitRoleBot, боты создаются через makeBot,
    // ИИ-агенты — через makeAgent (оба шаблона ограничены концептом RoleBot), и все
    // хранятся как SharedPtr<Player> (конвертация Derived → Base).
    const std::vector<int> mafiaTeam = aliveInTeam(Team::Mafia);
    SharedPtr<HumanPlayer> human;
    for (int id = 1; id <= count; ++id) {
        const Role role = roleOf(id);
        const std::uint64_t seed = mixSeed(config_.seed, static_cast<std::uint64_t>(id));
        SharedPtr<Player> player;
        if (id == humanId_) {
            human = MakeShared<HumanPlayer>(id, role, humanInput_);
            player = human;
        } else {
            const bool agent = isAgent(id);
            visitRoleBot(role, [&]<RoleBot Bot>() {
                const auto join = [&mafiaTeam](auto& created) {
                    if constexpr (requires { created->setTeam(mafiaTeam); }) {
                        created->setTeam(mafiaTeam);  // только мафиози и ниндзя
                    }
                };
                if (agent) {
                    auto created = makeAgent<Bot>(id, seed, agentCharacteristic_.at(id), llm_);
                    join(created);
                    player = std::move(created);
                } else {
                    auto created = makeBot<Bot>(id, seed);
                    join(created);
                    player = std::move(created);
                }
            });
        }
        players_.push_back(std::move(player));
    }

    // Игроки — отдельные нити (по умолчанию) или корутины в нити ведущего.
    if (config_.coroutines) {
        auto runtime = MakeShared<CoroutineRuntime>(CoroutineRuntime::readerFor(humanInput_));
        if (human) {
            human->useAsyncInput(runtime->input());
        }
        runtime_ = std::move(runtime);
    } else {
        runtime_ = MakeShared<ThreadRuntime>();
    }
    runtime_->start(players_);
}

void Moderator::chooseAgents() {
    const int wanted = config_.aiAgents >= 0 ? config_.aiAgents : roleConfig_.ai.count.value_or(0);
    if (wanted <= 0) {
        return;
    }
    // Свой генератор: выбор агентов не сдвигает случайность самой партии.
    Rng agentRng(mixSeed(config_.seed, kAgentSeedSalt));
    std::vector<int> candidates = allExcept(std::views::iota(1, config_.players + 1), humanId_);
    agentRng.shuffle(candidates);
    candidates.resize(std::min(candidates.size(), static_cast<std::size_t>(wanted)));
    std::ranges::sort(candidates);
    agentIds_ = std::move(candidates);

    // Характеристики — случайные и разные, пока их хватает (из YAML или встроенные);
    // тем же отдельным генератором, что и выбор агентов.
    std::vector<std::string> characteristics =
        roleConfig_.ai.characteristics.empty() ? defaultCharacteristics() : roleConfig_.ai.characteristics;
    agentRng.shuffle(characteristics);
    for (std::size_t i = 0; i < agentIds_.size(); ++i) {
        agentCharacteristic_[agentIds_[i]] = characteristics[i % characteristics.size()];
    }
    if (!llm_) {
        llm_ = makeLlmClient(llmOptionsFromEnvironment(roleConfig_.ai.model));
    }
}

bool Moderator::isAgent(int id) const {
    return contains(agentIds_, id);
}

// Квоту запросов к API на ход раздаёт ведущий в своей нити по порядку номеров:
// сначала каждому живому агенту по первому запросу, затем остаток — на повторы
// (не больше одного на агента). Распределение детерминировано и одинаково в
// режимах нитей и корутин; неиспользованное после хода возвращается в квоту.
std::map<int, int> Moderator::grantAiCalls() {
    if (aiQuotaDay_ != state_->day) {
        aiQuotaDay_ = state_->day;
        aiQuotaLeft_ = kAiCallsPerRound;
    }
    std::map<int, int> granted;
    for (int attempt = 0; attempt < AgentBrain::kMaxAttempts; ++attempt) {
        for (int id : agentIds_) {
            if (aiQuotaLeft_ > 0 && isAlive(id)) {
                ++granted[id];
                --aiQuotaLeft_;
            }
        }
    }
    return granted;
}

void Moderator::returnAiCalls(const std::map<int, int>& granted, const std::map<int, Response>& answers) {
    for (const auto& [id, calls] : granted) {
        const auto answer = answers.find(id);
        const int used = answer == answers.end() ? 0 : std::clamp(answer->second.aiCalls, 0, calls);
        aiQuotaLeft_ += calls - used;
    }
}

void Moderator::announceStart() {
    const int count = config_.players;
    log_->announce("=================== МАФИЯ ===================");
    log_->announce(compositionText());
    if (!roleConfig_.source.empty()) {
        log_->announce("Набор ролей: " + roleConfig_.source + ".");
    }
    for (const auto& message : skippedRoles_) {
        log_->announce(message);
    }
    log_->announce(config_.openAnnouncements
                       ? "Объявления открытые: ведущий раскрывает роли погибших и подробности ночи."
                       : "Объявления закрытые: ведущий называет только сторону погибшего (мирный/мафия).");
    log_->announce(config_.interactive ? "Режим: вы играете вместе с ботами." : "Режим: играют только боты.");
    log_->announce("Сид партии: " + std::to_string(config_.seed) + " (повторить: --seed " +
                   std::to_string(config_.seed) + ")");
    if (!config_.logDir.empty()) {
        // Ошибка создания каталога печатается предупреждением; партия идёт без файлов.
        fileLog_ = MakeShared<FileLog>(
            std::filesystem::path(config_.logDir) / ("game_" + std::to_string(config_.seed)),
            [this](const std::string& warning) { log_->announce(warning); });
        sinks_.push_back(fileLog_);
        if (fileLog_->active()) {
            log_->announce("Логи партии: " + fileLog_->directory().string());
        }
    }

    if (!agentIds_.empty()) {
        log_->announce("ИИ-агенты: " + names(agentIds_) + " (модель " + llm_->model() + ").");
    }

    log_->secret("Раздача ролей:");
    for (int id = 1; id <= count; ++id) {
        log_->secret("  " + nameWithRole(id));
    }
    for (int id : agentIds_) {
        log_->secret(nameWithRole(id) + " — ИИ-агент, характеристика: «" + agentCharacteristic_.at(id) + "»");
    }
    if (!agentIds_.empty() && !llm_->available()) {
        log_->secret("API языковой модели недоступно (" + llm_->unavailableReason() +
                     ") — агенты играют как обычные боты.");
    }

    emit([&](GameEventSink& sink) {
        sink.gameStarted(GameSetupInfo{.players = count,
                                       .seed = config_.seed,
                                       .openAnnouncements = config_.openAnnouncements,
                                       .humanId = humanId_,
                                       .roles = state_->roles,
                                       .skippedRoles = skippedRoles_,
                                       .rolesSource = roleConfig_.source.empty() ? "встроенный (классический)"
                                                                                 : roleConfig_.source,
                                       .aiAgents = agentIds_,
                                       .aiModel = agentIds_.empty() ? std::string() : llm_->model()});
    });

    if (humanId_ != 0) {
        const Role role = roleOf(humanId_);
        log_->announce("");
        log_->personal("Вы — Игрок " + std::to_string(humanId_) + ". Ваша роль: " + std::string(roleName(role)) + ".");
        log_->personal(roleHint(role));
        if (teamOf(role) == Team::Mafia) {
            const std::vector<int> partners = allExcept(aliveInTeam(Team::Mafia), humanId_);
            std::string text;
            for (int partner : partners) {
                text += (text.empty() ? "" : ", ") + name(partner) + (roleOf(partner) == Role::Ninja ? " (ниндзя)" : "");
            }
            log_->personal(partners.empty() ? "Вы единственный мафиози." : "Ваши напарники: " + text + ".");
        }
    }
}

// Например: «Игроков: 7. Мафия: 2, комиссар, доктор, маньяк, мирные жители: 2.»
std::string Moderator::compositionText() const {
    const auto count = [this](Role role) { return std::ranges::count(composition_, role); };
    std::string text = "Игроков: " + std::to_string(config_.players) +
                       ". Мафия: " + std::to_string(count(Role::Mafia) + count(Role::Ninja));
    if (count(Role::Ninja) > 0) {
        text += " (из них ниндзя: " + std::to_string(count(Role::Ninja)) + ")";
    }
    const auto isSpecial = [](Role role) {
        return role != Role::Mafia && role != Role::Ninja && role != Role::Civilian;
    };
    for (Role role : composition_ | std::views::filter(isSpecial)) {
        text += ", " + std::string(roleNameLower(role));
    }
    return text + ", мирные жители: " + std::to_string(count(Role::Civilian)) + ".";
}

// Например: «Просыпаются мафия, комиссар, доктор и маньяк.»
std::string Moderator::nightRolesText() const {
    std::vector<std::string> awake;
    const auto present = [this](Role role) { return std::ranges::find(composition_, role) != composition_.end(); };
    if (present(Role::Mafia) || present(Role::Ninja)) {
        awake.emplace_back("мафия");
    }
    for (Role role : kNightOrder | std::views::filter(present)) {
        awake.emplace_back(roleNameLower(role));
    }
    std::string list;
    for (std::size_t i = 0; i < awake.size(); ++i) {
        list += (i == 0 ? "" : (i + 1 == awake.size() ? " и " : ", ")) + awake[i];
    }
    return (awake.size() == 1 ? "Просыпается " : "Просыпаются ") + list + ".";
}

// --------------------------------------------------------------------- фазы

void Moderator::startDay() {
    ++state_->day;
    log_->announce("");
    log_->announce("================ День " + std::to_string(state_->day) + " ================");
    log_->announce("Город просыпается.");
}

void Moderator::discussion() {
    emit([&](GameEventSink& sink) { sink.dayStarted(state_->day, aliveIds()); });
    log_->announce("Живы: " + names(aliveIds()));
    log_->announce("--- Обсуждение ---");

    const auto view = snapshot();
    const std::map<int, int> aiCalls = grantAiCalls();
    Batch batch;
    for (int id : aliveIds()) {
        batch.emplace_back(id, Request{.type = RequestType::Discussion,
                                       .view = view,
                                       .targets = aliveExcept(id),
                                       .aiCalls = aiCalls.contains(id) ? aiCalls.at(id) : 0});
    }
    // Все высказываются одновременно (каждый в своей нити) по одному снимку,
    // ведущий зачитывает реплики по порядку номеров.
    const std::map<int, Response> statements = ask(std::move(batch));
    returnAiCalls(aiCalls, statements);
    for (const auto& [id, response] : statements) {
        // Реплика ИИ-агента звучит его словами; у ботов — шаблон ведущего.
        const std::string text = response.message.empty() ? statementText(id, response) : response.message;
        state_->history.push_back(PublicEvent{.type = EventType::Statement,
                                              .day = state_->day,
                                              .actor = id,
                                              .target = response.target,
                                              .statement = response.action,
                                              .confident = response.confident,
                                              .text = text});
        log_->announce(name(id) + ": «" + text + "»");
        if (!response.reason.empty()) {
            log_->secret(nameWithRole(id) + " — " + response.reason);
        }
        emit([&](GameEventSink& sink) { sink.statement(state_->day, id, response, text); });
        pause(kLinePause);
    }
}

void Moderator::voting() {
    log_->announce("--- Голосование ---");

    const auto view = snapshot();
    const std::map<int, int> aiCalls = grantAiCalls();
    Batch batch;
    for (int id : aliveIds()) {
        batch.emplace_back(id, Request{.type = RequestType::DayVote,
                                       .view = view,
                                       .targets = aliveExcept(id),
                                       .aiCalls = aiCalls.contains(id) ? aiCalls.at(id) : 0});
    }

    // Голоса зачитываются по одному, с паузой.
    const std::map<int, Response> ballots = ask(std::move(batch));
    returnAiCalls(aiCalls, ballots);
    for (const auto& [id, response] : ballots) {
        const std::string said = response.message.empty() ? "" : " — «" + response.message + "»";
        if (response.action == ActionType::Abstain) {  // так ходит только ИИ-агент
            state_->history.push_back(
                PublicEvent{.type = EventType::Abstain, .day = state_->day, .actor = id, .text = response.message});
            log_->announce("  " + name(id) + " воздерживается" + said);
        } else {
            state_->history.push_back(PublicEvent{
                .type = EventType::Vote, .day = state_->day, .actor = id, .target = response.target});
            log_->announce("  " + name(id) + " голосует против: " + name(response.target) + said);
        }
        if (!response.reason.empty()) {
            log_->secret(nameWithRole(id) + " — " + response.reason);
        }
        emit([&](GameEventSink& sink) { sink.voteCast(state_->day, id, response); });
        pause(kLinePause);
    }

    // Подсчёт голосов: строки по убыванию голосов, при равенстве — по номеру цели.
    const std::vector<VoteRow> rows = tallyVotes(ballots);
    log_->announce("Итоги голосования:");
    for (const auto& [target, voters] : rows) {
        log_->announce("  " + name(target) + " — " + std::to_string(voters.size()) + " " +
                       plural(voters.size(), "голос", "голоса", "голосов"));
    }
    pause(kLinePause);

    const std::vector<int> leaders = voteLeaders(rows);
    if (leaders.empty()) {  // все воздержались — бывает только с ИИ-агентами
        log_->announce("  голосов нет");
        log_->announce("Все воздержались — сегодня никто не выбывает.");
        emit([&](GameEventSink& sink) { sink.votingFinished(state_->day, rows, 0, false); });
        return;
    }
    int executed = leaders.front();
    if (leaders.size() > 1) {
        executed = rng_.pick(leaders);
        log_->announce("Поровну голосов у: " + names(leaders) + ". Ведущий подбрасывает монетку…");
        pause(kLinePause);
    }
    log_->announce("По итогам голосования из игры выбывает " + name(executed) + ".");
    emit([&](GameEventSink& sink) { sink.votingFinished(state_->day, rows, executed, leaders.size() > 1); });
    eliminate(executed, {DeathCause::Execution});
}

void Moderator::night() {
    pause(kRoundPause);
    log_->announce("");
    log_->announce("================ Ночь " + std::to_string(state_->day) + " ================");
    log_->announce("Город засыпает. " + nightRolesText());
    if (humanAlive() && roleOf(humanId_) == Role::Civilian) {
        log_->personal("Вы мирный житель — ночью вы спите.");
    }
    const int night = state_->day;
    emit([&](GameEventSink& sink) { sink.nightStarted(night); });

    // Все ночные роли ходят одновременно, каждая в своей нити. Допустимые цели
    // задаёт класс роли (концепт NightRole); мирный житель ночью не ходит.
    const auto view = snapshot();
    const NightContext context{.alive = aliveIds(), .roles = state_->roles, .lastHeal = state_->lastHeal};
    Batch batch;
    for (int id : aliveIds()) {
        std::vector<int> targets;
        visitNightRole(roleOf(id), [&]<NightRole R>() { targets = R::nightTargets(context, id); });
        if (!targets.empty()) {
            batch.emplace_back(id, Request{.type = RequestType::Night, .view = view, .targets = std::move(targets)});
        }
    }
    const auto answers = ask(std::move(batch));

    // Ходы собираются по правилам классов ролей (collectNightMove ограничен NightRole).
    const NightMoves moves = collectNightMoves(answers, [this](int id) { return roleOf(id); });
    const std::vector<int> mafia = toVector(moves.mafia | std::views::transform(&NightMoves::Move::actor));
    const NightOutcome outcome = resolveNight(moves, state_->roles);

    // Мафия: общее решение большинством, при равенстве решает босс
    // (живой мафиози с наименьшим номером).
    if (!mafia.empty()) {
        std::string proposalText;
        for (int id : mafia) {
            const Response& response = answers.at(id);
            proposalText += (proposalText.empty() ? "" : ", ") + name(id) + " → " + name(response.target);
            log_->secret(nameWithRole(id) + " предлагает убить: " + name(response.target) +
                         (response.reason.empty() ? "" : " — " + response.reason));
        }
        const int victim = *outcome.mafiaVictim;
        log_->secret("Решение мафии (босс — " + name(mafia.front()) + "): жертва " + name(victim));
        if (humanAlive() && teamOf(roleOf(humanId_)) == Team::Mafia && mafia.size() > 1) {
            log_->personal("Предложения мафии: " + proposalText + ". Решение: " + name(victim) + ".");
        }
    }

    for (const auto& [id, response] : answers) {
        emit([&](GameEventSink& sink) { sink.nightAction(night, id, response); });
        const std::string reason = response.reason.empty() ? "" : " — " + response.reason;
        switch (roleOf(id)) {
            case Role::Commissar:
                if (response.action == ActionType::Shoot) {
                    log_->secret(nameWithRole(id) + " стреляет: " + name(response.target) + reason);
                } else {
                    const CheckResult check = *outcome.check;
                    log_->secret(nameWithRole(id) + " проверяет: " + name(check.target) + " → " +
                                 std::string(sideName(checkSide(roleOf(check.target)))) + reason);
                    if (id == humanId_) {
                        log_->personal("Результат проверки: " + name(check.target) + " — " +
                                       (check.isMafia ? "мафия!" : "не мафия."));
                    } else {
                        runtime_->send(id, Request{.type = RequestType::Info, .check = check});
                    }
                }
                break;
            case Role::Doctor:
                log_->secret(nameWithRole(id) + " лечит: " + name(response.target) + reason);
                break;
            case Role::Maniac:
                log_->secret(nameWithRole(id) + " убивает: " + name(response.target) + reason);
                break;
            case Role::Bodyguard:
                log_->secret(nameWithRole(id) + " охраняет: " + name(response.target) + reason);
                break;
            case Role::Journalist: {
                const CompareResult comparison = *outcome.comparison;
                const std::string verdict = comparison.same ? "одинаковый статус" : "разный статус";
                log_->secret(nameWithRole(id) + " сравнивает: " + name(comparison.first) + " и " +
                             name(comparison.second) + " → " + verdict + reason);
                if (id == humanId_) {
                    log_->personal("Результат сравнения: " + name(comparison.first) + " и " +
                                   name(comparison.second) + " — " + verdict + ".");
                } else {
                    runtime_->send(id, Request{.type = RequestType::Info, .comparison = comparison});
                }
                break;
            }
            default:
                break;
        }
    }
    state_->lastHeal = moves.doctor ? std::optional(moves.doctor->target) : std::nullopt;
    if (outcome.counterKilled) {
        log_->secret("Телохранитель отвечает выстрелом: " + name(*outcome.counterKilled));
    }
    emit([&](GameEventSink& sink) { sink.nightResolved(night, outcome); });

    // Ночь проходит; утром объявляются спасения и погибшие.
    pause(kNightPause);
    startDay();
    for (const auto& [target, causes] : outcome.attacks) {
        const bool guarded = outcome.guarded.contains(target);
        if (!guarded && !outcome.healed.contains(target)) {
            continue;
        }
        const std::string saver = guarded ? "телохранителем" : "доктором";
        log_->secret(name(target) + (guarded ? " прикрыт " : " спасён ") + saver + " (" + causeList(causes) + ")");
        if (config_.openAnnouncements) {
            log_->announce(name(target) + " был атакован (" + causeList(causes) + "), но " +
                           (guarded ? "телохранитель его прикрыл." : "доктор его спас."));
            state_->history.push_back(
                PublicEvent{.type = EventType::Saved, .day = state_->day, .target = target, .causes = causes});
        }
    }
    if (outcome.deaths.empty()) {
        log_->announce("Этой ночью никто не погиб.");
    }
    for (int victim : outcome.deaths) {
        eliminate(victim, outcome.attacks.at(victim));
        pause(kLinePause);
    }
}

void Moderator::finish(Winner winner) {
    pause(kRoundPause);
    log_->announce("");
    log_->announce("=============== Игра окончена ===============");
    switch (winner) {
        case Winner::Town:
            log_->announce("Победили мирные жители: мафия и маньяк выбыли.");
            break;
        case Winner::Mafia:
            log_->announce("Победила мафия.");
            break;
        case Winner::Maniac:
            log_->announce("Победил маньяк.");
            break;
        case Winner::Draw:
        case Winner::Aborted:
            log_->announce("Ничья.");
            break;
    }
    log_->announce("Сыграно дней: " + std::to_string(state_->day) + ". Роли игроков:");
    for (int id = 1; id <= config_.players; ++id) {
        log_->announce("  " + nameWithRole(id) + (isAlive(id) ? " — жив" : " — выбыл"));
    }
    if (humanId_ != 0) {
        const Team team = teamOf(roleOf(humanId_));
        const bool won = (winner == Winner::Town && team == Team::Town) ||
                         (winner == Winner::Mafia && team == Team::Mafia) ||
                         (winner == Winner::Maniac && team == Team::Maniac);
        log_->personal(won ? "Вы победили!" : "Вы проиграли.");
    }
    emit([&](GameEventSink& sink) { sink.gameEnded(winner, state_->day); });
    log_->flushSecrets();
}

void Moderator::pause(std::chrono::milliseconds duration) {
    if (!config_.pauses) {
        return;
    }
    log_->flush();
    std::this_thread::sleep_for(duration);
}

void Moderator::shutdown() noexcept {
    if (runtime_) {
        runtime_->stop();
    }
}

// --------------------------------------------- обмен сообщениями с игроками

std::map<int, Response> Moderator::ask(Batch batch) {
    std::map<int, Request> pending;
    for (auto& [id, request] : batch) {
        request.seq = nextSeq_++;
        if (id == humanId_) {
            promptHuman(request);
        }
        runtime_->send(id, request);
        pending.emplace(id, std::move(request));
    }

    std::map<int, Response> answers;
    while (answers.size() < pending.size()) {
        Response response = runtime_->receive();
        const auto it = pending.find(response.player);
        if (it == pending.end() || it->second.seq != response.seq || answers.contains(response.player)) {
            continue;
        }
        if (response.player == humanId_) {
            if (response.endOfInput) {
                throw InputClosed{};
            }
            if (!response.valid || !isValid(response, it->second, roleOf(humanId_))) {
                log_->personal("Не получилось: " + (response.error.empty() ? "недопустимый выбор" : response.error) +
                               ". Попробуйте ещё раз.");
                log_->inputPrompt();
                it->second.seq = nextSeq_++;
                runtime_->send(humanId_, it->second);
                continue;
            }
        }
        answers.emplace(response.player, std::move(response));
    }

    // Ответы ботов проверяются по порядку номеров, чтобы запасной случайный
    // выбор не зависел от того, какая нить ответила первой.
    for (auto& [id, response] : answers) {
        const Request& request = pending.at(id);
        if (id != humanId_ && (!response.valid || !isValid(response, request, roleOf(id)))) {
            log_->secret(name(id) + " дал недопустимый ответ (" + response.error + "), ведущий выбирает за него");
            response = fallback(request, roleOf(id));
            response.player = id;
        }
    }
    return answers;
}

bool Moderator::isValid(const Response& response, const Request& request, Role role) const {
    const bool targetOk = contains(request.targets, response.target);
    switch (request.type) {
        case RequestType::Discussion:
            return response.action == ActionType::ClaimInnocent || (response.action == ActionType::Accuse && targetOk);
        case RequestType::DayVote:
            // Воздержаться может только ИИ-агент.
            return (response.action == ActionType::Vote && targetOk) ||
                   (response.action == ActionType::Abstain && isAgent(response.player));
        case RequestType::Night: {
            bool valid = false;  // у роли без ночного хода ночной ответ всегда неверен
            visitNightRole(role, [&]<NightRole R>() { valid = R::isValidNightMove(response, request); });
            return valid;
        }
        default:
            return false;
    }
}

Response Moderator::fallback(const Request& request, Role role) {
    Response response;
    if (request.type == RequestType::Discussion) {
        response.action = ActionType::ClaimInnocent;
    } else if (request.type == RequestType::DayVote) {
        if (!request.targets.empty()) {
            response.action = ActionType::Vote;
            response.target = rng_.pick(request.targets);
        }
    } else {
        visitNightRole(role, [&]<NightRole R>() { response = R::fallbackNightMove(request, rng_); });
    }
    response.reason = "случайный выбор ведущего";
    return response;
}

void Moderator::promptHuman(const Request& request) {
    const Role role = roleOf(humanId_);
    switch (request.type) {
        case RequestType::Discussion:
            log_->personal("Ваше слово. Введите номер игрока, которого обвиняете, или 0 — «я мирный житель».");
            break;
        case RequestType::DayVote:
            log_->personal("Ваш голос. Введите номер игрока, которого хотите выгнать из города.");
            break;
        case RequestType::Night:
            switch (role) {
                case Role::Mafia:
                case Role::Ninja:
                    log_->personal("Мафия выбирает жертву. Введите номер игрока.");
                    break;
                case Role::Commissar:
                    log_->personal("Ход комиссара: «п N» — проверить игрока N, «в N» — выстрелить в игрока N.");
                    break;
                case Role::Doctor:
                    log_->personal(state_->lastHeal ? "Кого лечите? Прошлой ночью вы лечили " +
                                                          name(*state_->lastHeal) + " — его сейчас нельзя."
                                                    : "Кого лечите этой ночью? Себя тоже можно.");
                    break;
                case Role::Maniac:
                    log_->personal("Кого убиваете этой ночью? Введите номер игрока.");
                    break;
                case Role::Journalist:
                    log_->personal("Ход журналиста: введите два номера через пробел — кого сравнить (например «2 5»).");
                    break;
                case Role::Bodyguard:
                    log_->personal("Кого охраняете этой ночью? Введите номер игрока.");
                    break;
                case Role::Civilian:
                    break;
            }
            break;
        default:
            break;
    }
    std::string available;
    for (int id : request.targets) {
        available += (available.empty() ? "" : ", ") + std::to_string(id);
    }
    log_->personal("Доступные номера: " + available);
    log_->inputPrompt();
}

// ------------------------------------------------------------------- правила

void Moderator::eliminate(int id, const std::vector<DeathCause>& causes) {
    const auto index = static_cast<std::size_t>(id - 1);
    const Role role = roleOf(id);
    const Side side = announcedSide(role);
    const bool executed = causes.size() == 1 && causes.front() == DeathCause::Execution;

    state_->alive[index] = false;
    PlayerPublicInfo& info = state_->publicInfo[index];
    info.alive = false;
    info.revealedSide = side;

    PublicEvent event{.type = EventType::Death, .day = state_->day, .target = id, .side = side};
    if (config_.openAnnouncements) {
        info.revealedRole = role;
        event.role = role;
        event.causes = causes;
        log_->announce(executed ? name(id) + " покидает игру. Роль: " + std::string(roleName(role)) + "."
                                : "Этой ночью погиб " + name(id) + " (" + std::string(roleName(role)) +
                                      "). Стреляли: " + causeList(causes) + ".");
    } else {
        event.causes = {executed ? DeathCause::Execution : DeathCause::Unknown};
        log_->announce(executed ? name(id) + " покидает игру — " + std::string(sideName(side)) + "."
                                : "Этой ночью погиб " + name(id) + " — " + std::string(sideName(side)) + ".");
    }
    state_->history.push_back(std::move(event));
    emit([&](GameEventSink& sink) { sink.playerEliminated(state_->day, id, causes); });

    if (id == humanId_) {
        log_->personal("Вы выбыли. Дальше партия идёт без вас — можете наблюдать.");
    }
}

// Победа по реальным лагерям: ниндзя считается мафией, маньяк — сам за себя.
std::optional<Winner> Moderator::checkWinner() const {
    const std::vector<int> alive = aliveIds();
    return decideWinner(countTeams(alive | std::views::transform([this](int id) { return roleOf(id); })));
}

std::string Moderator::statementText(int speaker, const Response& response) const {
    const auto variant = static_cast<std::size_t>(state_->day + speaker) % 3;
    if (response.action != ActionType::Accuse) {
        static constexpr std::array<const char*, 3> kInnocent = {
            "Я мирный житель, мне скрывать нечего.", "Я честный горожанин, ищите мафию в другом месте.",
            "Я за мирных. Давайте внимательно слушать друг друга."};
        return kInnocent[variant];
    }
    const std::string target = name(response.target);
    if (response.confident) {
        return "Я уверен: " + target + " — мафия! Голосуйте за него.";
    }
    const std::array<std::string, 3> accusations = {"Мне кажется, " + target + " — мафия.",
                                                    "Я подозреваю, что " + target + " играет за мафию.",
                                                    target + " ведёт себя подозрительно."};
    return accusations[variant];
}

// ------------------------------------------------------------------- утилиты

SharedPtr<const GameView> Moderator::snapshot() const {
    auto view = MakeShared<GameView>();
    view->day = state_->day;
    view->openAnnouncements = config_.openAnnouncements;
    view->mafiaTotal = static_cast<int>(
        std::ranges::count_if(composition_, [](Role role) { return teamOf(role) == Team::Mafia; }));
    view->ninjaTotal = static_cast<int>(std::ranges::count(composition_, Role::Ninja));
    view->players = state_->publicInfo;
    view->events = state_->history;
    return view;
}

// Живые игроки по возрастанию номеров.
std::vector<int> Moderator::aliveIds() const {
    return toVector(std::views::iota(1, config_.players + 1) |
                    std::views::filter([this](int id) { return isAlive(id); }));
}

// Кандидаты для обвинения, голосования и большинства ночных ходов: все живые, кроме самого игрока.
std::vector<int> Moderator::aliveExcept(int id) const {
    return allExcept(aliveIds(), id);
}

std::vector<int> Moderator::aliveInTeam(Team team) const {
    const std::vector<int> alive = aliveIds();
    return toVector(alive | std::views::filter([this, team](int id) { return teamOf(roleOf(id)) == team; }));
}

bool Moderator::isAlive(int id) const {
    return state_->alive[static_cast<std::size_t>(id - 1)];
}

Role Moderator::roleOf(int id) const {
    return state_->roles[static_cast<std::size_t>(id - 1)];
}

bool Moderator::humanAlive() const {
    return humanId_ != 0 && isAlive(humanId_);
}

std::string Moderator::name(int id) const {
    return "Игрок " + std::to_string(id) + (id == humanId_ ? " (вы)" : "");
}

std::string Moderator::nameWithRole(int id) const {
    return name(id) + " [" + std::string(roleName(roleOf(id))) + "]";
}

std::string Moderator::names(const std::vector<int>& ids) const {
    std::string text;
    for (int id : ids) {
        text += (text.empty() ? "" : ", ") + name(id);
    }
    return text;
}

}  // namespace mafia
