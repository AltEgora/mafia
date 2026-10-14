// Тесты ИИ-агента на моке LlmClient — без сети и без ключа API.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "mafia/ai_agent.hpp"
#include "mafia/game_rules.hpp"
#include "mafia/json.hpp"
#include "mafia/llm_client.hpp"
#include "mafia/moderator.hpp"
#include "mafia/ranges_util.hpp"
#include "test_framework.hpp"

using namespace mafia;

namespace {

// Мок клиента: запоминает запросы и отвечает заданной функцией. Потокобезопасен —
// в режиме нитей агенты обращаются к нему одновременно.
class MockLlm final : public LlmClient {
public:
    using Answer = std::function<LlmReply(const LlmRequest&)>;

    explicit MockLlm(Answer answer, bool available = true) : answer_(std::move(answer)), available_(available) {}

    bool available() const override { return available_; }
    std::string unavailableReason() const override { return "мок выключен"; }
    std::string model() const override { return "mock-model"; }

    LlmReply complete(const LlmRequest& request) override {
        std::lock_guard lock(mutex_);
        requests_.push_back(request);
        return answer_(request);
    }

    std::vector<LlmRequest> requests() const {
        std::lock_guard lock(mutex_);
        return requests_;
    }

private:
    Answer answer_;
    bool available_;
    mutable std::mutex mutex_;
    std::vector<LlmRequest> requests_;
};

SharedPtr<MockLlm> replying(std::string text) {
    return MakeShared<MockLlm>([text](const LlmRequest&) { return LlmReply{.ok = true, .text = text, .error = {}}; });
}

// Снимок: игроки 1..6, мёртвые — из списка; день — day.
SharedPtr<const GameView> view(std::vector<int> dead = {}, int day = 1) {
    auto snapshot = MakeShared<GameView>();
    snapshot->day = day;
    for (int id = 1; id <= 6; ++id) {
        snapshot->players.push_back(PlayerPublicInfo{.id = id, .alive = !contains(dead, id)});
    }
    return snapshot;
}

Request discussion(int self, std::vector<int> dead = {}, int day = 1) {
    auto snapshot = view(dead, day);
    std::vector<int> targets;
    for (int id = 1; id <= 6; ++id) {
        if (id != self && snapshot->isAlive(id)) {
            targets.push_back(id);
        }
    }
    return Request{.type = RequestType::Discussion, .view = snapshot, .targets = targets, .aiCalls = AgentBrain::kMaxAttempts};
}

Request voting(int self, std::vector<int> dead = {}) {
    Request request = discussion(self, std::move(dead));
    request.type = RequestType::DayVote;
    return request;
}

constexpr int kAgent = 2;
constexpr std::uint64_t kSeed = 77;

}  // namespace

// ------------------------------------------------------- ответы модели

TEST(ValidReplyBecomesAgentMove) {
    auto llm = replying(R"({"action": "accuse", "message": "Игрок 3 путается\nв показаниях!", "target": 3,
                            "reasoning": "он обвинил меня без причины"})");
    auto agent = makeAgent<Civilian>(kAgent, kSeed, "никому не доверяет", llm);
    const Response response = agent->discuss(discussion(kAgent));
    CHECK(response.action == ActionType::Accuse);
    CHECK_EQ(response.target, 3);
    CHECK_EQ(response.message, std::string("Игрок 3 путается в показаниях!"));  // одна строка
    CHECK_EQ(response.reason, std::string("ИИ: он обвинил меня без причины"));
    const std::vector<LlmRequest> requests = llm->requests();
    CHECK_EQ(requests.size(), std::size_t{1});
    CHECK(!requests[0].system.empty());
    CHECK_EQ(requests[0].player, kAgent);
}

TEST(FencedReplyAndDefendAndVote) {
    auto defend = makeAgent<Civilian>(kAgent, kSeed, "осторожный",
                                      replying("```json\n{\"action\":\"defend\",\"message\":\"Я свой.\","
                                               "\"target\":null,\"reasoning\":\"\"}\n```"));
    const Response claim = defend->discuss(discussion(kAgent));
    CHECK(claim.action == ActionType::ClaimInnocent);
    CHECK_EQ(claim.target, 0);
    CHECK_EQ(claim.reason, std::string("ИИ: без пояснений"));

    auto voter = makeAgent<Civilian>(kAgent, kSeed, "логик",
                                     replying(R"({"action":"vote","message":"","target":5,"reasoning":"давит"})"));
    const Response ballot = voter->vote(voting(kAgent));
    CHECK(ballot.action == ActionType::Vote);
    CHECK_EQ(ballot.target, 5);
    CHECK(ballot.message.empty());
}

// При любой ошибке агент ходит ровно как обычный бот той же роли и сида.
TEST(BrokenJsonFallsBackToBot) {
    for (std::uint64_t seed = 0; seed < 10; ++seed) {
        auto agent = makeAgent<Civilian>(kAgent, seed, "плохо врёт", replying(R"({"action": "accuse", "target": 3)"));
        auto bot = makeBot<Civilian>(kAgent, seed);
        const Request request = discussion(kAgent);
        const Response fromAgent = agent->discuss(request);
        const Response fromBot = bot->discuss(request);
        CHECK(fromAgent.action == fromBot.action);
        CHECK_EQ(fromAgent.target, fromBot.target);
        CHECK(fromAgent.message.empty());
        // Битый JSON — повод для одного повтора; второй ответ такой же — ходит бот.
        CHECK(fromAgent.reason.starts_with("ИИ не ответил (ответ отклонён дважды: битый JSON"));
        CHECK(fromAgent.reason.ends_with(fromBot.reason));
        CHECK_EQ(fromAgent.aiCalls, 2);
    }
}

TEST(InvalidRepliesFallBack) {
    const auto reasonFor = [](const std::string& reply, const Request& request) {
        auto agent = makeAgent<Civilian>(kAgent, kSeed, "логик", replying(reply));
        return request.type == RequestType::Discussion ? agent->discuss(request).reason : agent->vote(request).reason;
    };
    // Мёртвый игрок.
    CHECK(reasonFor(R"({"action":"accuse","message":"","target":4,"reasoning":""})", discussion(kAgent, {4}))
              .find("не живой игрок (4)") != std::string::npos);
    // Сам агент и несуществующий игрок.
    CHECK(reasonFor(R"({"action":"vote","message":"","target":2,"reasoning":""})", voting(kAgent))
              .find("цель 2 недоступна") != std::string::npos);
    CHECK(reasonFor(R"({"action":"vote","message":"","target":9,"reasoning":""})", voting(kAgent))
              .find("не живой игрок (9)") != std::string::npos);
    // Действие не по этапу, цели нет, тип поля неверный, не JSON вовсе.
    CHECK(reasonFor(R"({"action":"vote","message":"","target":3,"reasoning":""})", discussion(kAgent))
              .find("не подходит этапу") != std::string::npos);
    CHECK(reasonFor(R"({"action":"accuse","message":"","target":null,"reasoning":""})", discussion(kAgent))
              .find("нет цели") != std::string::npos);
    CHECK(reasonFor(R"({"action":"vote","message":"","target":"три","reasoning":""})", voting(kAgent))
              .find("номером игрока") != std::string::npos);
    CHECK(reasonFor("Я думаю, мафия — игрок 3.", discussion(kAgent)).find("нет JSON") != std::string::npos);
}

TEST(TenCallsPerRoundAtMost) {
    auto llm = replying(R"({"action":"accuse","message":"Подозреваю 3","target":3,"reasoning":"x"})");
    auto agent = makeAgent<Civilian>(kAgent, kSeed, "эмоциональный", llm);
    int fromModel = 0;
    for (int i = 0; i < 15; ++i) {
        const Response response = agent->discuss(discussion(kAgent, {}, 1));
        fromModel += response.message == "Подозреваю 3" ? 1 : 0;
        if (i >= 10) {
            CHECK(response.reason.find("лимит 10 запросов за раунд") != std::string::npos);
        }
    }
    CHECK_EQ(llm->requests().size(), std::size_t{10});
    CHECK_EQ(fromModel, 10);
    CHECK_EQ(agent->brain().totalCalls(), 10);

    agent->discuss(discussion(kAgent, {}, 2));  // новый раунд — лимит обнулился
    CHECK_EQ(llm->requests().size(), std::size_t{11});
    CHECK(llm->requests().back().round == 2);
}

TEST(NoPermitMeansNoCall) {
    auto llm = replying(R"({"action":"accuse","message":"","target":3,"reasoning":""})");
    auto agent = makeAgent<Civilian>(kAgent, kSeed, "логик", llm);
    Request request = discussion(kAgent);
    request.aiCalls = 0;  // квоту раунда ведущий уже раздал
    CHECK(agent->discuss(request).reason.find("квота") != std::string::npos);
    CHECK(llm->requests().empty());
}

// Нет ключа, нет libcurl: агент молча играет ботом — даже без пометки в причине.
TEST(UnavailableApiIsSilentFallback) {
    auto llm = MakeShared<MockLlm>([](const LlmRequest&) { return LlmReply{}; }, false);
    auto agent = makeAgent<Civilian>(kAgent, kSeed, "логик", llm);
    auto bot = makeBot<Civilian>(kAgent, kSeed);
    for (int i = 0; i < 3; ++i) {
        const Response a = agent->discuss(discussion(kAgent));
        const Response b = bot->discuss(discussion(kAgent));
        CHECK(a.action == b.action && a.target == b.target && a.reason == b.reason);
    }
    CHECK(llm->requests().empty());

    auto offline = makeAgent<Civilian>(kAgent, kSeed, "логик",
                                       makeLlmClient(LlmOptions{.apiKey = ""}));  // без ключа
    CHECK(offline->discuss(discussion(kAgent)).reason.find("ИИ") == std::string::npos);
}

TEST(ClientErrorsFallBack) {
    auto timeout = makeAgent<Civilian>(kAgent, kSeed, "логик", MakeShared<MockLlm>([](const LlmRequest&) {
        return LlmReply{.ok = false, .text = {}, .error = "сеть: Timeout was reached"};
    }));
    CHECK(timeout->discuss(discussion(kAgent)).reason.find("Timeout was reached") != std::string::npos);
    CHECK_EQ(timeout->brain().totalCalls(), 1);  // сеть — не ошибка в ответе модели: без повтора

    auto throwing = makeAgent<Civilian>(kAgent, kSeed, "логик", MakeShared<MockLlm>([](const LlmRequest&) -> LlmReply {
        throw std::runtime_error("обрыв");
    }));
    CHECK(throwing->discuss(discussion(kAgent)).reason.find("ошибка клиента: обрыв") != std::string::npos);
}

// ------------------------------------- язык, совпадение реплики и хода, abstain

TEST(EnglishWordsMakeReplyInvalid) {
    const auto failure = [](const std::string& reply, const Request& request) {
        return parseAgentReply(reply, request).failure;
    };
    CHECK(failure(R"({"action":"defend","message":"Обвинять unfairly не стоит.","target":null,"reasoning":""})",
                  discussion(kAgent))
              .find("английское слово «unfairly»") != std::string::npos);
    CHECK(failure(R"({"action":"vote","message":"Голосую против игрока 3.","target":3,"reasoning":"отвожу suspicion"})",
                  voting(kAgent))
              .find("английское слово «suspicion»") != std::string::npos);
    // Одиночная латинская буква словом не считается; кириллица — всегда можно.
    CHECK(parseAgentReply(R"({"action":"vote","message":"Голосую против игрока 3, план Б.","target":3,"reasoning":"x — улика"})",
                          voting(kAgent))
              .response.has_value());
}

TEST(NamedPlayersInMessage) {
    CHECK(namedPlayers("Игрок 3 путается") == std::vector<int>{3});
    CHECK(namedPlayers("голосую против Игрока №5, а игроком 2 займёмся завтра") == (std::vector<int>{2, 5}));
    CHECK(namedPlayers("игроки 2, 4 и 6 молчат") == (std::vector<int>{2, 4, 6}));
    CHECK(namedPlayers("ИГРОК 7!") == std::vector<int>{7});
    CHECK(namedPlayers("3 игрока молчат, игроки ведут себя странно").empty());
    CHECK(namedPlayers("Подозреваю 3").empty());
}

// Реплика должна совпадать с ходом: назван только тот, кто в target.
TEST(MessageMustMatchTarget) {
    const auto parse = [](const std::string& reply, const Request& request) { return parseAgentReply(reply, request); };
    // Партия студента: «голосовать против Игрока 4 преждевременно», а голос — против 5.
    const AgentDecision mismatch = parse(
        R"({"action":"vote","message":"Голосовать против игрока 4 преждевременно.","target":5,"reasoning":""})",
        voting(kAgent));
    CHECK(!mismatch.response);
    CHECK(mismatch.failure.find("назван игрок 4, а target — 5") != std::string::npos);
    CHECK(!parse(R"({"action":"vote","message":"Игрок 4 молчит, голосую против игрока 5.","target":5,"reasoning":""})",
                 voting(kAgent))
               .response);
    CHECK(!parse(R"({"action":"accuse","message":"Игрок 3 врёт!","target":4,"reasoning":""})", discussion(kAgent))
               .response);
    CHECK(parse(R"({"action":"vote","message":"Голосую против Игрока 5.","target":5,"reasoning":""})", voting(kAgent))
              .response.has_value());
    CHECK(parse(R"({"action":"vote","message":"Голосую по совести.","target":5,"reasoning":""})", voting(kAgent))
              .response.has_value());
}

TEST(AbstainVote) {
    auto llm = replying(R"({"action":"abstain","message":"Оснований голосовать пока нет.","target":null,"reasoning":"улик нет"})");
    auto agent = makeAgent<Civilian>(kAgent, kSeed, "миротворец", llm);
    const Response ballot = agent->vote(voting(kAgent));
    CHECK(ballot.action == ActionType::Abstain);
    CHECK_EQ(ballot.target, 0);
    CHECK_EQ(ballot.message, std::string("Оснований голосовать пока нет."));
    const json::Value prompt = json::parse(llm->requests().at(0).prompt);
    CHECK(prompt.find("allowed_actions")->dump() == R"(["vote","abstain"])");
    CHECK(llm->requests().at(0).system.find("abstain") != std::string::npos);

    const auto failure = [](const std::string& reply, const Request& request) {
        return parseAgentReply(reply, request).failure;
    };
    CHECK(failure(R"({"action":"abstain","message":"","target":3,"reasoning":""})", voting(kAgent))
              .find("target должен быть null") != std::string::npos);
    CHECK(failure(R"({"action":"abstain","message":"","target":null,"reasoning":""})", discussion(kAgent))
              .find("не подходит этапу") != std::string::npos);
    CHECK(failure(R"({"action":"abstain","message":"Игрок 3 вне подозрений.","target":null,"reasoning":""})",
                  voting(kAgent))
              .find("при abstain в message назван игрок 3") != std::string::npos);
    // При defend и abstain можно назвать себя, но не других.
    CHECK(parseAgentReply(R"({"action":"defend","message":"Я, игрок 2, ни при чём.","target":null,"reasoning":""})",
                          discussion(kAgent), kAgent)
              .response.has_value());
    CHECK(failure(R"({"action":"defend","message":"Я не мафия, а вот игрок 4 — да.","target":null,"reasoning":""})",
                  discussion(kAgent))
              .find("при defend в message назван игрок 4") != std::string::npos);

    // Воздержавшиеся не учитываются в подсчёте; воздержались все — строк нет.
    const std::map<int, Response> ballots{{1, Response{.action = ActionType::Vote, .target = 3}},
                                          {2, Response{.action = ActionType::Abstain}},
                                          {4, Response{.action = ActionType::Vote, .target = 3}}};
    CHECK(tallyVotes(ballots) == (std::vector<VoteRow>{VoteRow{3, {1, 4}}}));
    CHECK(tallyVotes({{2, Response{.action = ActionType::Abstain}}}).empty());
}

// Ответившая модель видна в логе: запрошенная — «модель X», запасная — отдельно.
TEST(LogShowsAnsweringModel) {
    const auto agentWith = [](std::string model, std::string text) {
        return makeAgent<Civilian>(kAgent, kSeed, "логик", MakeShared<MockLlm>([model, text](const LlmRequest&) {
            return LlmReply{.ok = true, .text = text, .error = {}, .model = model};
        }));
    };
    const std::string good = R"({"action":"accuse","message":"Игрок 3 путается.","target":3,"reasoning":"он молчал"})";
    CHECK_EQ(agentWith("mock-model", good)->discuss(discussion(kAgent)).reason,
             std::string("ИИ (модель mock-model): он молчал"));
    CHECK_EQ(agentWith("google/gemma-4-31b-it:free", good)->discuss(discussion(kAgent)).reason,
             std::string("ИИ (ответила запасная модель google/gemma-4-31b-it:free вместо mock-model): он молчал"));

    // И в причине отказа: чей ответ отклонён.
    const Response rejected = agentWith("liquid/lfm-2.5-2.6b:free", "User Safety: safe")->discuss(discussion(kAgent));
    CHECK(rejected.reason.starts_with("ИИ не ответил (ответ отклонён дважды: в ответе нет JSON-объекта "
                                      "(ответила запасная модель liquid/lfm-2.5-2.6b:free вместо mock-model)"));

    // Системный промпт: не повторять свои реплики дословно.
    auto llm = replying(good);
    makeAgent<Civilian>(kAgent, kSeed, "логик", llm)->discuss(discussion(kAgent));
    CHECK(llm->requests().at(0).system.find("не повторяй свои прошлые реплики") != std::string::npos);
}

// ----------------------------------------------------------------- повтор

namespace {

constexpr std::string_view kEnglishReply =
    R"({"action":"accuse","message":"Игрок 3 is suspicious.","target":3,"reasoning":""})";
constexpr std::string_view kGoodReply =
    R"({"action":"accuse","message":"Игрок 3 путается в показаниях.","target":3,"reasoning":"он молчал"})";

// Первый ответ — с английским словом, ответ на повтор (есть correction) — правильный.
SharedPtr<MockLlm> fixedOnRetry() {
    return MakeShared<MockLlm>([](const LlmRequest& request) {
        return LlmReply{.ok = true, .text = std::string(request.correction.empty() ? kEnglishReply : kGoodReply), .error = {}};
    });
}

}  // namespace

TEST(RetryWithErrorExplained) {
    auto llm = fixedOnRetry();
    auto agent = makeAgent<Civilian>(kAgent, kSeed, "логик", llm);
    const Response response = agent->discuss(discussion(kAgent));
    CHECK_EQ(response.target, 3);
    CHECK_EQ(response.message, std::string("Игрок 3 путается в показаниях."));
    CHECK_EQ(response.reason, std::string("ИИ со второй попытки: он молчал"));
    CHECK_EQ(response.aiCalls, 2);

    const std::vector<LlmRequest> requests = llm->requests();
    CHECK_EQ(requests.size(), std::size_t{2});
    CHECK(requests[0].correction.empty());
    CHECK_EQ(requests[1].prompt, requests[0].prompt);  // тот же промпт, плюс диалог об ошибке
    CHECK_EQ(requests[1].previousReply, std::string(kEnglishReply));
    CHECK(requests[1].correction.find("английское слово «is»") != std::string::npos);

    // В API повтор уходит диалогом: system, user, assistant (отклонённый ответ), user (пояснение).
    const json::Value body = json::parse(buildChatRequest(LlmOptions{}, requests[1]));
    const json::Array& messages = body.find("messages")->asArray();
    CHECK_EQ(messages.size(), std::size_t{4});
    CHECK_EQ(messages[2].find("role")->asString(), std::string("assistant"));
    CHECK_EQ(messages[2].find("content")->asString(), std::string(kEnglishReply));
    CHECK_EQ(messages[3].find("role")->asString(), std::string("user"));
    CHECK(messages[3].find("content")->asString().starts_with("Твой ответ не принят: английское слово"));
}

TEST(TwoBadRepliesMeanBotMove) {
    auto llm = replying(R"({"action":"vote","message":"Голосую против игрока 4.","target":5,"reasoning":""})");
    auto agent = makeAgent<Civilian>(kAgent, kSeed, "упрямый", llm);
    auto bot = makeBot<Civilian>(kAgent, kSeed);
    const Response fromAgent = agent->vote(voting(kAgent));
    const Response fromBot = bot->vote(voting(kAgent));
    CHECK_EQ(fromAgent.target, fromBot.target);
    CHECK(fromAgent.message.empty());
    CHECK(fromAgent.reason.starts_with("ИИ не ответил (ответ отклонён дважды: в message назван игрок 4"));
    CHECK(fromAgent.reason.ends_with("ход бота: " + fromBot.reason));
    CHECK_EQ(llm->requests().size(), std::size_t{2});
}

// Повтор входит в лимит: нет второго разрешения ведущего или исчерпан лимит
// агента (10 за раунд) — повтора нет, ходит бот, причина в логе.
TEST(RetryCountsAgainstLimits) {
    auto single = fixedOnRetry();
    auto agent = makeAgent<Civilian>(kAgent, kSeed, "логик", single);
    Request oneCall = discussion(kAgent);
    oneCall.aiCalls = 1;
    const Response response = agent->discuss(oneCall);
    CHECK(response.message.empty());
    CHECK(response.reason.find("повтора нет — квота запросов раунда исчерпана") != std::string::npos);
    CHECK_EQ(response.aiCalls, 1);
    CHECK_EQ(single->requests().size(), std::size_t{1});

    auto llm = fixedOnRetry();
    auto busy = makeAgent<Civilian>(kAgent, kSeed, "логик", llm);
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(busy->discuss(discussion(kAgent)).aiCalls, 2);  // 8 запросов: ответ, повтор
    }
    CHECK(busy->discuss(discussion(kAgent)).reason.starts_with("ИИ со второй попытки"));  // 10-й запрос
    const Response limited = busy->discuss(discussion(kAgent));
    CHECK(limited.reason.find("лимит 10 запросов за раунд") != std::string::npos);
    CHECK_EQ(limited.aiCalls, 0);
    CHECK_EQ(llm->requests().size(), std::size_t{10});

    // Лимит кончился посреди хода: первый ответ отклонён, на повтор запроса нет.
    auto edge = fixedOnRetry();
    auto almost = makeAgent<Civilian>(kAgent, kSeed, "логик", edge);
    for (int i = 0; i < 9; ++i) {
        Request first = discussion(kAgent);
        first.aiCalls = 1;
        almost->discuss(first);  // 9 запросов, по одному
    }
    const Response cut = almost->discuss(discussion(kAgent));
    CHECK(cut.reason.find("ответ отклонён: английское слово «is»") != std::string::npos);
    CHECK(cut.reason.find("повтор: лимит 10 запросов за раунд") != std::string::npos);
    CHECK_EQ(edge->requests().size(), std::size_t{10});
}

// --------------------------------------------------------------- промпт

TEST(PromptHoldsOnlyWhatPlayerSaw) {
    auto llm = replying(R"({"action":"defend","message":"","target":null,"reasoning":""})");
    auto agent = makeAgent<Commissar>(3, kSeed, "никому не доверяет", llm);
    agent->deliverInfo(Request{.type = RequestType::Info, .check = CheckResult{4, true}});

    auto snapshot = MakeShared<GameView>();
    snapshot->day = 2;
    for (int id = 1; id <= 5; ++id) {
        snapshot->players.push_back(PlayerPublicInfo{.id = id, .alive = id != 2});
    }
    snapshot->players[1].revealedSide = Side::Mafia;  // закрытые объявления: только сторона
    snapshot->events = {
        PublicEvent{.type = EventType::Statement, .day = 1, .actor = 1, .statement = ActionType::ClaimInnocent,
                    .text = "Я мирный житель."},
        PublicEvent{.type = EventType::Vote, .day = 1, .actor = 1, .target = 2},
        PublicEvent{.type = EventType::Death, .day = 2, .target = 2, .causes = {DeathCause::Unknown}, .side = Side::Mafia},
    };
    agent->discuss(Request{.type = RequestType::Discussion, .view = snapshot, .targets = {1, 4, 5}, .aiCalls = AgentBrain::kMaxAttempts});

    const json::Value prompt = json::parse(llm->requests().at(0).prompt);
    CHECK_EQ(prompt.find("you")->find("player")->asNumber(), 3.0);
    CHECK_EQ(prompt.find("you")->find("role")->asString(), std::string("Комиссар"));
    CHECK_EQ(prompt.find("you")->find("team")->asString(), std::string("мирные"));
    CHECK_EQ(prompt.find("you")->find("characteristic")->asString(), std::string("никому не доверяет"));
    CHECK_EQ(prompt.find("phase")->find("stage")->asString(), std::string("discussion"));
    CHECK(prompt.find("alive_players")->dump() == "[1,3,4,5]");
    CHECK(prompt.find("allowed_targets")->dump() == "[1,4,5]");
    CHECK(prompt.find("eliminated")->dump() == R"([{"player":2,"side":"мафия","role":null}])");
    CHECK(prompt.find("private_knowledge")->dump() == R"(["Твоя проверка: игрок 4 — мафия"])");
    const json::Array& history = prompt.find("history")->asArray();
    CHECK_EQ(history.size(), std::size_t{2});  // два раунда
    CHECK_EQ(history[0].find("events")->asArray()[0].find("text")->asString(), std::string("Я мирный житель."));
    const json::Value& death = history[1].find("events")->asArray()[0];
    CHECK_EQ(death.find("how")->asString(), std::string("погиб ночью"));
    CHECK(death.find("role")->isNull());              // роль не раскрыта
    CHECK(death.find("killed_by")->asArray().empty());  // кто убил — тоже
}

TEST(MafiaAgentKnowsTeamOnly) {
    auto llm = replying(R"({"action":"defend","message":"","target":null,"reasoning":""})");
    auto agent = makeAgent<Mafioso>(1, kSeed, "плохо врёт", llm);
    agent->setTeam({1, 5});
    agent->discuss(discussion(1));
    const json::Value prompt = json::parse(llm->requests().at(0).prompt);
    CHECK(prompt.find("private_knowledge")->dump() == R"(["Твои напарники по мафии: игроки 5"])");
    CHECK_EQ(prompt.find("you")->find("team")->asString(), std::string("мафия"));
}

// Напарник остаётся допустимой целью: пойти против него — право модели, если это
// осознанный ход ради победы мафии; системный промпт запрещает лишь делать это
// из-за характеристики.
TEST(MafiaAgentMayTargetPartnerOnPurpose) {
    auto llm = replying(R"({"action":"accuse","message":"Игрок 5 подозрителен","target":5,"reasoning":"отвожу подозрения"})");
    auto agent = makeAgent<Mafioso>(1, kSeed, "никому не доверяет", llm);
    agent->setTeam({1, 5});
    const Response response = agent->discuss(discussion(1));
    const json::Value prompt = json::parse(llm->requests().at(0).prompt);
    CHECK(prompt.find("allowed_targets")->dump() == "[2,3,4,5,6]");
    CHECK_EQ(response.target, 5);
    CHECK_EQ(response.message, std::string("Игрок 5 подозрителен"));
    const std::string system = llm->requests().at(0).system;
    CHECK(system.find("не обвиняй своих напарников") != std::string::npos);
    CHECK(system.find("осознанный ход ради победы мафии") != std::string::npos);
}

// ----------------------------------------------------- концепты и ночь

static_assert(DayPlayer<AiAgent<Civilian>> && !NightPlayer<AiAgent<Civilian>>, "мирный-агент ночью спит");
static_assert(NightPlayer<AiAgent<Doctor>> && NightPlayer<AiAgent<Ninja>>);
static_assert(RoleClass<AiAgent<Commissar>> && AiAgent<Commissar>::kRole == Role::Commissar);
static_assert(!RoleBot<AiAgent<Civilian>>, "агента создаёт makeAgent, а не makeBot");
template <class Agent>
constexpr bool canSetTeam = requires(Agent& agent) { agent.setTeam(std::vector<int>{1, 2}); };
static_assert(canSetTeam<AiAgent<Mafioso>> && canSetTeam<AiAgent<Ninja>>, "команда — только у мафии");
static_assert(!canSetTeam<AiAgent<Doctor>> && !canSetTeam<AiAgent<Civilian>>);

TEST(NightMoveIsBotMove) {
    auto llm = replying(R"({"action":"accuse","message":"","target":3,"reasoning":""})");
    for (std::uint64_t seed = 0; seed < 10; ++seed) {
        auto agent = makeAgent<Doctor>(4, seed, "логик", llm);
        auto bot = makeBot<Doctor>(4, seed);
        const Request night{.type = RequestType::Night, .view = view(), .targets = {1, 2, 3, 4, 5}, .aiCalls = AgentBrain::kMaxAttempts};
        const int healed = agent->act(night).target;
        CHECK_EQ(healed, bot->act(night).target);
        // Свой ночной ход агент помнит — он попадёт в личные сведения промпта.
        CHECK(agent->brain().privateNotes() == std::vector<std::string>{"Ночь 1: ты лечил игрока " + std::to_string(healed)});
    }
    CHECK(llm->requests().empty());  // ночью модель не спрашивают
}

// ------------------------------------------------------- протокол API

TEST(ChatRequestShape) {
    const LlmOptions options{.model = "openrouter/free", .apiKey = "секрет"};
    const json::Value body = json::parse(buildChatRequest(options, LlmRequest{.system = "правила", .prompt = "{\"x\":1}"}));
    CHECK_EQ(body.find("model")->asString(), std::string("openrouter/free"));
    const json::Array& messages = body.find("messages")->asArray();
    CHECK_EQ(messages.size(), std::size_t{2});
    CHECK_EQ(messages.at(0).find("role")->asString(), std::string("system"));
    CHECK_EQ(messages.at(0).find("content")->asString(), std::string("правила"));
    CHECK_EQ(messages.at(1).find("role")->asString(), std::string("user"));
    CHECK_EQ(messages.at(1).find("content")->asString(), std::string("{\"x\":1}"));
    CHECK_EQ(body.find("response_format")->find("type")->asString(), std::string("json_object"));
    CHECK(body.find("max_tokens")->asNumber() > 0);
    CHECK(body.find("models") == nullptr);                   // запасных моделей не задано
    CHECK(body.find("reasoning") == nullptr);                // длина рассуждения не задана
    CHECK(body.dump().find("секрет") == std::string::npos);  // ключ — только в заголовке

    const LlmOptions openRouter{.model = "a:free", .fallbackModels = {"b:free", "c:free"}, .reasoningEffort = "low"};
    const json::Value routed = json::parse(buildChatRequest(openRouter, LlmRequest{}));
    CHECK_EQ(routed.find("models")->dump(), std::string(R"(["a:free","b:free","c:free"])"));
    CHECK_EQ(routed.find("reasoning")->dump(), std::string(R"({"effort":"low"})"));
}

TEST(ChatResponseParsing) {
    const LlmReply ok = parseChatResponse(
        R"({"choices":[{"finish_reason":"stop","message":{"role":"assistant","content":"{\"a\":1}"}}]})", 200);
    CHECK(ok.ok);
    CHECK_EQ(ok.text, std::string("{\"a\":1}"));
    CHECK(ok.model.empty());

    // Какая модель ответила — из поля model, даже если ответ не годится.
    const LlmReply routed = parseChatResponse(
        R"({"model":"google/gemma-4-31b-it:free","choices":[{"message":{"content":"{}"}}]})", 200);
    CHECK_EQ(routed.model, std::string("google/gemma-4-31b-it:free"));
    CHECK(answeredByFallback(routed, "nvidia/nemotron-3-super-120b-a12b:free"));
    CHECK(!answeredByFallback(routed, "google/gemma-4-31b-it:free"));
    CHECK(!answeredByFallback(LlmReply{}, "любая"));  // модель неизвестна — не запасная
    CHECK_EQ(parseChatResponse(R"({"model":"m:free","choices":[{"finish_reason":"length","message":{"content":null}}]})", 200)
                 .model,
             std::string("m:free"));

    // Рассуждение в content отрезается; content из частей склеивается.
    CHECK_EQ(parseChatResponse(R"({"choices":[{"message":{"content":"<think>{не то}</think>\n{\"b\":2}"}}]})", 200).text,
             std::string("\n{\"b\":2}"));
    CHECK_EQ(parseChatResponse(R"({"choices":[{"message":{"content":[{"type":"text","text":"{\"c\":"},{"type":"text","text":"3}"}]}}]})",
                               200)
                 .text,
             std::string("{\"c\":3}"));

    CHECK(parseChatResponse(R"({"choices":[{"finish_reason":"length","message":{"content":"{\"a\""}}]})", 200)
              .error.find("обрезан") != std::string::npos);
    CHECK(parseChatResponse(R"({"choices":[{"finish_reason":"content_filter","message":{"content":null}}]})", 200)
              .error.find("фильтр") != std::string::npos);
    CHECK(parseChatResponse(R"({"choices":[{"message":{"content":"","refusal":"не буду"}}]})", 200)
              .error.find("отказалась") != std::string::npos);
    CHECK(parseChatResponse(R"({"choices":[{"message":{"content":null}}]})", 200).error.find("пустой") !=
          std::string::npos);
    CHECK(parseChatResponse(R"({"choices":[]})", 200).error.find("нет choices") != std::string::npos);

    const LlmReply denied = parseChatResponse(R"({"error":{"message":"No auth credentials found","code":401}})", 401);
    CHECK(!denied.ok);
    CHECK_EQ(denied.error, std::string("HTTP 401: No auth credentials found"));
    CHECK_EQ(parseChatResponse(R"({"error":{"message":"Rate limit exceeded: free-models-per-min"}})", 429).error,
             std::string("HTTP 429: Rate limit exceeded: free-models-per-min"));
    // OpenRouter может прислать ошибку поставщика с кодом 200.
    CHECK_EQ(parseChatResponse(R"({"error":{"message":"Provider returned error","code":502}})", 200).error,
             std::string("ошибка API: Provider returned error"));
    CHECK(parseChatResponse("<html>502</html>", 502).error.find("не JSON") != std::string::npos);
}

TEST(OptionsFromEnvironment) {
    // Ключ OpenRouter не уходит на чужой адрес; без ключа OpenRouter недоступен.
    unsetenv("MAFIA_LLM_API_KEY");
    unsetenv("MAFIA_LLM_MODEL");
    unsetenv("MAFIA_LLM_BASE_URL");
    setenv("OPENROUTER_API_KEY", "or-key", 1);
    LlmOptions options = llmOptionsFromEnvironment("из-yaml");
    CHECK_EQ(options.baseUrl, std::string(kDefaultLlmBaseUrl));
    CHECK_EQ(options.apiKey, std::string("or-key"));
    CHECK_EQ(options.model, std::string("из-yaml"));
    CHECK(options.fallbackModels.empty());  // модель выбрана вручную — не подменяем
    CHECK_EQ(options.reasoningEffort, std::string("low"));

    setenv("MAFIA_LLM_BASE_URL", "http://localhost:11434/v1/", 1);
    setenv("MAFIA_LLM_MODEL", "llama3.2", 1);
    options = llmOptionsFromEnvironment("из-yaml");
    CHECK_EQ(options.baseUrl, std::string("http://localhost:11434/v1"));
    CHECK(options.apiKey.empty());
    CHECK_EQ(options.model, std::string("llama3.2"));
    CHECK(options.fallbackModels.empty());  // поля models и reasoning понимает только OpenRouter
    CHECK(options.reasoningEffort.empty());
    if (builtWithCurl()) {
        CHECK(makeLlmClient(options)->available());  // локальной Ollama ключ не нужен
    }

    unsetenv("MAFIA_LLM_BASE_URL");
    unsetenv("MAFIA_LLM_MODEL");
    unsetenv("OPENROUTER_API_KEY");
    options = llmOptionsFromEnvironment("");
    CHECK_EQ(options.model, std::string(kDefaultLlmModel));
    CHECK_EQ(options.fallbackModels.size(), kDefaultFallbackModels.size());
    CHECK(!makeLlmClient(options)->available());
}

// --------------------------------------------- партия с агентами на моке

namespace {

// Мок «игрока»: обвиняет и голосует против первого допустимого игрока.
SharedPtr<MockLlm> scriptedLlm() {
    return MakeShared<MockLlm>([](const LlmRequest& request) {
        const json::Value prompt = json::parse(request.prompt);
        const int target = static_cast<int>(prompt.find("allowed_targets")->asArray().at(0).asNumber());
        const bool vote = prompt.find("phase")->find("stage")->asString() == "vote";
        const std::string text = json::Value(json::Object{
            {"action", vote ? "vote" : "accuse"},
            {"message", (vote ? "Голосую против " : "Подозреваю игрока ") + std::to_string(target)},
            {"target", target},
            {"reasoning", "проверочный мок"}}).dump();
        return LlmReply{.ok = true, .text = text, .error = {}};
    });
}

std::string playGame(SharedPtr<LlmClient> llm, int players, int agents, bool coroutines, bool fullLog,
                     const std::string& logDir = {}, std::uint64_t seed = 11, RoleConfig roles = defaultRoleConfig()) {
    GameConfig config;
    config.players = players;
    config.seed = seed;
    config.pauses = false;
    config.fullLog = fullLog;
    config.coroutines = coroutines;
    config.aiAgents = agents;
    config.logDir = logDir;
    std::istringstream input;
    std::ostringstream output;
    Moderator moderator(config, std::move(roles), input, output, std::move(llm));
    moderator.run();
    return output.str();
}

}  // namespace

TEST(GameQuotaIsTenCallsPerRound) {
    auto llm = scriptedLlm();
    const std::string output = playGame(llm, 14, 13, false, false);
    std::map<int, int> perRound;
    for (const LlmRequest& request : llm->requests()) {
        ++perRound[request.round];
    }
    CHECK(!perRound.empty());
    for (const auto& [round, calls] : perRound) {
        CHECK(calls <= 10);
    }
    CHECK(perRound.begin()->second == 10);  // в первый день агентов больше квоты — квота выбрана целиком
    CHECK(output.find("Подозреваю игрока") != std::string::npos);
}

TEST(GameWithMockMatchesAcrossModes) {
    const std::string threads = playGame(scriptedLlm(), 9, 4, false, true);
    const std::string coroutines = playGame(scriptedLlm(), 9, 4, true, true);
    CHECK(threads == coroutines);
    CHECK(threads.find("ИИ-агенты: ") != std::string::npos);
}

TEST(ReasoningOnlyInFullLog) {
    const std::string brief = playGame(scriptedLlm(), 9, 4, false, false);
    const std::string full = playGame(scriptedLlm(), 9, 4, false, true);
    CHECK(brief.find("Подозреваю игрока") != std::string::npos);  // реплики — в обычном логе
    CHECK(brief.find("Голосую против") != std::string::npos);     // и голоса
    CHECK(brief.find("проверочный мок") == std::string::npos);    // рассуждение — нет
    CHECK(full.find("ИИ: проверочный мок") != std::string::npos);
}

TEST(AgentMovesInFileLogs) {
    std::random_device device;
    const auto directory = std::filesystem::temp_directory_path() / ("mafia_ai_log_" + std::to_string(device()));
    playGame(scriptedLlm(), 9, 4, false, false, directory.string());
    std::ifstream round(directory / "game_11" / "round_01.txt");
    std::stringstream text;
    text << round.rdbuf();
    CHECK(text.str().find("Подозреваю игрока") != std::string::npos);
    CHECK(text.str().find("мотив: ИИ: проверочный мок") != std::string::npos);
    std::ifstream summary(directory / "game_11" / "summary.txt");
    std::stringstream summaryText;
    summaryText << summary.rdbuf();
    CHECK(summaryText.str().find("ИИ-агенты: игроки") != std::string::npos);
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

// Каждому агенту — случайная характеристика из встроенных десяти, в партии все разные.
namespace {

// Характеристики агентов, как их видит полный лог.
std::vector<std::string> characteristicsIn(const std::string& output) {
    std::vector<std::string> found;
    const std::string marker = "характеристика: «";
    for (std::size_t at = output.find(marker); at != std::string::npos; at = output.find(marker, at + 1)) {
        const std::size_t begin = at + marker.size();
        found.push_back(output.substr(begin, output.find("»", begin) - begin));
    }
    return found;
}

}  // namespace

// Десять характеристик живут в configs/ai_agents.yaml: каждому агенту — случайная,
// в партии все разные. Без YAML у всех агентов нейтральная из кода.
TEST(CharacteristicsComeFromYaml) {
    const RoleConfig yaml = loadRoleConfig(std::filesystem::path(MAFIA_SOURCE_DIR) / "configs" / "ai_agents.yaml");
    CHECK_EQ(yaml.ai.characteristics.size(), std::size_t{10});
    std::set<std::string> seen;
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        auto offline = MakeShared<MockLlm>([](const LlmRequest&) { return LlmReply{}; }, false);
        const std::vector<std::string> inGame = characteristicsIn(playGame(offline, 9, 3, false, true, {}, seed, yaml));
        CHECK_EQ(inGame.size(), std::size_t{3});
        CHECK_EQ(std::set<std::string>(inGame.begin(), inGame.end()).size(), std::size_t{3});
        for (const std::string& characteristic : inGame) {
            CHECK(contains(yaml.ai.characteristics, characteristic));
        }
        seen.insert(inGame.begin(), inGame.end());
    }
    CHECK(seen.size() > 3);  // разные партии — разные наборы

    auto offline = MakeShared<MockLlm>([](const LlmRequest&) { return LlmReply{}; }, false);
    CHECK(characteristicsIn(playGame(offline, 9, 3, false, true)) ==
          std::vector<std::string>(3, "обычный игрок без особых привычек"));
}

// Повторы делят с первыми запросами общую квоту ведущего — 10 за раунд.
TEST(GameRetriesShareRoundQuota) {
    auto llm = MakeShared<MockLlm>([](const LlmRequest& request) {
        if (request.correction.empty()) {
            return LlmReply{.ok = true, .text = "не JSON", .error = {}};
        }
        return scriptedLlm()->complete(request);
    });
    const std::string output = playGame(llm, 9, 4, false, true);
    std::map<int, int> perRound;
    int retries = 0;
    for (const LlmRequest& request : llm->requests()) {
        ++perRound[request.round];
        retries += request.correction.empty() ? 0 : 1;
    }
    CHECK_EQ(perRound.at(1), 8);  // 4 агента: ответ и повтор у каждого
    for (const auto& [round, calls] : perRound) {
        CHECK(calls <= 10);
    }
    CHECK(retries > 0);
    CHECK(output.find("ИИ со второй попытки") != std::string::npos);
    CHECK(playGame(llm, 9, 4, true, true) == output);  // нити = корутины и с повторами
}

// Все агенты, все воздерживаются: голосов нет — днём никто не выбывает.
TEST(GameWithAbstainingAgents) {
    const auto abstaining = [] {
        return MakeShared<MockLlm>([](const LlmRequest& request) {
            const bool vote = json::parse(request.prompt).find("phase")->find("stage")->asString() == "vote";
            return LlmReply{.ok = true,
                            .text = vote ? R"({"action":"abstain","message":"Воздержусь.","target":null,"reasoning":"улик нет"})"
                                         : R"({"action":"defend","message":"Я ни при чём.","target":null,"reasoning":"x"})",
                            .error = {}};
        });
    };
    const std::string threads = playGame(abstaining(), 7, 7, false, true);
    CHECK(threads.find("воздерживается — «Воздержусь.»") != std::string::npos);
    CHECK(threads.find("Все воздержались — сегодня никто не выбывает.") != std::string::npos);
    CHECK(threads.find("Игра окончена") != std::string::npos);
    CHECK(threads == playGame(abstaining(), 7, 7, true, true));
}

// API недоступно: партия с агентами идёт как партия ботов.
TEST(GameWithoutApiPlaysLikeBots) {
    auto offline = MakeShared<MockLlm>([](const LlmRequest&) { return LlmReply{}; }, false);
    std::string withAgents = playGame(offline, 9, 4, false, false);
    const std::string withoutAgents = playGame({}, 9, 0, false, false);
    const std::size_t line = withAgents.find("ИИ-агенты: ");
    CHECK(line != std::string::npos);
    withAgents.erase(line, withAgents.find('\n', line) - line + 1);
    CHECK(withAgents == withoutAgents);
    CHECK(offline->requests().empty());
}

MAFIA_TEST_MAIN()
