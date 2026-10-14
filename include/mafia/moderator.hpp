#pragma once

#include <chrono>
#include <cstdint>
#include <istream>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "mafia/config.hpp"
#include "mafia/game_events.hpp"
#include "mafia/logger.hpp"
#include "mafia/messages.hpp"
#include "mafia/player.hpp"
#include "mafia/random.hpp"
#include "mafia/roles_config.hpp"
#include "mafia/shared_ptr.hpp"

namespace mafia {

class FileLog;
class LlmClient;
class PlayerRuntime;

// Полное состояние партии. Меняет его только ведущий (главная нить),
// игроки видят его через неизменяемые снимки GameView.
struct GameState {
    int day = 0;
    std::vector<Role> roles;  // индекс = id - 1
    std::vector<bool> alive;
    std::vector<PlayerPublicInfo> publicInfo;
    std::vector<PublicEvent> history;
    std::optional<int> lastHeal;  // кого доктор лечил прошлой ночью
};

// Ведущий: точка синхронизации. Рассылает запросы игрокам (нитям или корутинам), собирает
// ответы, применяет правила, печатает происходящее и сообщает о событиях
// подписчикам (файловому логу) — всё из главной нити.
class Moderator {
public:
    // llm — клиент языковой модели для ИИ-агентов; пусто — создаётся из переменных
    // окружения, если агенты заданы (--ai-agents или ai_agents в YAML).
    Moderator(const GameConfig& config, RoleConfig roles, std::istream& humanInput, std::ostream& out,
              SharedPtr<LlmClient> llm = {});
    ~Moderator();

    Moderator(const Moderator&) = delete;
    Moderator& operator=(const Moderator&) = delete;

    Winner run();

private:
    using Batch = std::vector<std::pair<int, Request>>;

    void setup();
    void chooseAgents();
    bool isAgent(int id) const;
    std::map<int, int> grantAiCalls();
    void returnAiCalls(const std::map<int, int>& granted, const std::map<int, Response>& answers);
    void announceStart();
    void startDay();
    void discussion();
    void voting();
    void night();
    void finish(Winner winner);
    void shutdown() noexcept;
    void pause(std::chrono::milliseconds duration);

    std::map<int, Response> ask(Batch batch);
    bool isValid(const Response& response, const Request& request, Role role) const;
    Response fallback(const Request& request, Role role);
    void promptHuman(const Request& request);

    void eliminate(int id, const std::vector<DeathCause>& causes);
    std::optional<Winner> checkWinner() const;
    std::string statementText(int speaker, const Response& response) const;
    std::string compositionText() const;
    std::string nightRolesText() const;

    template <class Event>
    void emit(Event&& event) {
        for (auto& sink : sinks_) {
            event(*sink);
        }
    }

    SharedPtr<const GameView> snapshot() const;
    std::vector<int> aliveIds() const;
    std::vector<int> aliveExcept(int id) const;
    std::vector<int> aliveInTeam(Team team) const;
    bool isAlive(int id) const;
    Role roleOf(int id) const;
    bool humanAlive() const;
    std::string name(int id) const;
    std::string nameWithRole(int id) const;
    std::string names(const std::vector<int>& ids) const;

    GameConfig config_;
    RoleConfig roleConfig_;
    Rng rng_;
    std::istream& humanInput_;
    SharedPtr<Logger> log_;
    SharedPtr<GameState> state_;
    std::vector<Role> composition_;        // роли до перемешивания — для объявления состава
    std::vector<std::string> skippedRoles_;
    std::vector<SharedPtr<GameEventSink>> sinks_;  // подписчики на события партии
    SharedPtr<FileLog> fileLog_;                    // файловый лог, если задан --log-dir
    std::vector<SharedPtr<Player>> players_;
    // Нити или корутины игроков. Объявлен после players_: уничтожается раньше них.
    SharedPtr<PlayerRuntime> runtime_;
    int humanId_ = 0;
    SharedPtr<LlmClient> llm_;
    std::vector<int> agentIds_;                       // ИИ-агенты по возрастанию номеров
    std::map<int, std::string> agentCharacteristic_;  // номер агента → характеристика
    int aiQuotaDay_ = 0;                              // день, для которого считается квота
    int aiQuotaLeft_ = 0;                             // сколько запросов к API осталось в раунде
    std::uint64_t nextSeq_ = 1;
};

}  // namespace mafia
