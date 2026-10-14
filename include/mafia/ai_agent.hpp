#pragma once

// ИИ-агент — игрок, который днём (обсуждение и голосование) решает с помощью
// языковой модели, а ночью ходит как обычный бот своей роли. Если API
// недоступно (нет ключа, сеть, таймаут, плохой JSON, недопустимая цель,
// исчерпан лимит), агент молча отдаёт ход вложенному боту, и партия идёт дальше.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mafia/bots.hpp"
#include "mafia/json.hpp"
#include "mafia/llm_client.hpp"
#include "mafia/player_concepts.hpp"

namespace mafia {

// Кто такой агент: номер, роль и «характеристика» — как он играет.
struct AgentIdentity {
    int self = 0;
    Role role = Role::Civilian;
    std::string characteristic;
};

// Правила игры и формат ответа — системный промпт.
std::string agentSystemPrompt();

// Структурированный промпт: роль и характеристика, живые игроки, выбывшие,
// история раундов и личные сведения агента — только то, что игрок мог видеть.
json::Value buildAgentPrompt(const AgentIdentity& identity, const std::vector<std::string>& privateNotes,
                             const Request& request);

struct AgentDecision {
    std::optional<Response> response;  // nullopt — ответ модели не годится
    std::string failure;               // почему
};

// Разбирает ответ модели {action, message, target, reasoning} и проверяет его:
// действие подходит этапу, цель — живой игрок из допустимых; message и reasoning —
// только по-русски; в message назван только игрок из target (при defend и
// abstain — никто, кроме самого агента self).
AgentDecision parseAgentReply(std::string_view text, const Request& request, int self = 0);

// Номера игроков, названных в реплике: «игрок 3», «Игрока 5», «игроки 2, 4 и 6».
std::vector<int> namedPlayers(std::string_view text);

// Часть агента, не зависящая от роли: обращения к модели, повтор и лимит запросов.
class AgentBrain {
public:
    static constexpr int kMaxCallsPerRound = 10;
    // Попыток на один ход: если ответ модели не прошёл проверку, один повтор с
    // пояснением ошибки. Повтор входит в лимит запросов раунда.
    static constexpr int kMaxAttempts = 2;

    AgentBrain(AgentIdentity identity, SharedPtr<LlmClient> client);

    // Решение модели или nullopt — тогда ходит бот. В fallbackReason — причина;
    // пусто, если API недоступно вовсе (тогда переходим на бота без пометок).
    // Запросов делает не больше request.aiCalls (с повтором — до kMaxAttempts).
    std::optional<Response> decide(const Request& request, std::string& fallbackReason);

    void rememberInfo(const Request& info);  // проверки комиссара, сравнения журналиста
    void rememberNightMove(int night, const Response& move);  // свой ночной ход (его делает бот)
    void rememberTeam(const std::vector<int>& team);

    const AgentIdentity& identity() const noexcept { return identity_; }
    const std::vector<std::string>& privateNotes() const noexcept { return notes_; }
    int totalCalls() const noexcept { return totalCalls_; }
    int lastCalls() const noexcept { return lastCalls_; }  // запросов на последнем ходу

private:
    AgentIdentity identity_;
    SharedPtr<LlmClient> client_;
    std::vector<std::string> notes_;
    int round_ = 0;
    int callsThisRound_ = 0;
    int totalCalls_ = 0;
    int lastCalls_ = 0;
};

// Агент поверх бота своей роли. Методы discuss/vote/act — тот же интерфейс,
// что проверяют концепты; act есть только у ночных ролей, как и у ботов.
template <RoleBot Bot>
class AiAgent final : public Player {
public:
    static constexpr Role kRole = Bot::kRole;

    AiAgent(int id, std::uint64_t seed, std::string characteristic, SharedPtr<LlmClient> client)
        : Player(id, kRole), bot_(id, seed), brain_(AgentIdentity{id, kRole, std::move(characteristic)}, std::move(client)) {}

    Response discuss(const Request& request) {
        return decideOr(request, [&] { return bot_.discuss(request); });
    }

    Response vote(const Request& request) {
        return decideOr(request, [&] { return bot_.vote(request); });
    }

    // Ночью всегда ходит обычный бот. У мирного жителя ночного хода нет и у агента.
    Response act(const Request& request)
        requires NightPlayer<Bot>
    {
        Response move = bot_.act(request);
        brain_.rememberNightMove(request.view ? request.view->day : 0, move);
        return move;
    }

    // Команда мафии (только для мафиози и ниндзя).
    void setTeam(std::vector<int> team)
        requires requires(Bot& bot, std::vector<int> ids) { bot.setTeam(std::move(ids)); }
    {
        brain_.rememberTeam(team);
        bot_.setTeam(std::move(team));
    }

    Bot& bot() noexcept { return bot_; }
    const AgentBrain& brain() const noexcept { return brain_; }

protected:
    Response handle(const Request& request) override { return playTurn(*this, request); }

    void onInfo(const Request& request) override {
        brain_.rememberInfo(request);
        bot_.deliverInfo(request);
    }

private:
    template <class BotMove>
    Response decideOr(const Request& request, BotMove botMove) {
        std::string why;
        std::optional<Response> decided = brain_.decide(request, why);
        Response response = decided ? std::move(*decided) : botMove();
        if (!decided && !why.empty()) {
            response.reason = "ИИ не ответил (" + why + "), ход бота: " + response.reason;
        }
        response.aiCalls = brain_.lastCalls();  // неиспользованную квоту ведущий вернёт
        return response;
    }

    Bot bot_;
    AgentBrain brain_;
};

// Ведущий создаёт агентов только для классов ролей ботов (концепт RoleBot).
template <RoleBot Bot>
SharedPtr<AiAgent<Bot>> makeAgent(int id, std::uint64_t seed, std::string characteristic,
                                   SharedPtr<LlmClient> client) {
    return MakeShared<AiAgent<Bot>>(id, seed, std::move(characteristic), std::move(client));
}

}  // namespace mafia
