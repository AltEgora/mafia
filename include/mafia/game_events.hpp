#pragma once

// Общий интерфейс событий партии. Ведущий вызывает его только из главной
// нити — в тот момент, когда событие произошло. Нити игроков его не видят.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "mafia/game_rules.hpp"
#include "mafia/messages.hpp"
#include "mafia/night.hpp"
#include "mafia/roles.hpp"

namespace mafia {

std::string_view winnerName(Winner winner);   // «мирные жители», «мафия», ...
std::string_view causeName(DeathCause cause);  // «голосование», «мафия», «телохранитель», ...

struct GameSetupInfo {
    int players = 0;
    std::uint64_t seed = 0;
    bool openAnnouncements = false;
    int humanId = 0;                    // 0 — играют только боты
    std::vector<Role> roles;            // индекс = id - 1
    std::vector<std::string> skippedRoles;
    std::string rolesSource;            // файл ролей или «встроенный набор»
    std::vector<int> aiAgents;          // номера ИИ-агентов
    std::string aiModel;                // модель агентов
};

class GameEventSink {
public:
    virtual ~GameEventSink() = default;

    virtual void gameStarted(const GameSetupInfo&) {}
    virtual void dayStarted(int /*day*/, const std::vector<int>& /*alive*/) {}
    virtual void statement(int /*day*/, int /*speaker*/, const Response&, const std::string& /*text*/) {}
    virtual void voteCast(int /*day*/, int /*voter*/, const Response&) {}
    virtual void votingFinished(int /*day*/, const std::vector<VoteRow>& /*rows*/, int /*executed*/,
                                bool /*coinToss*/) {}
    virtual void nightStarted(int /*night*/) {}
    virtual void nightAction(int /*night*/, int /*actor*/, const Response&) {}
    virtual void nightResolved(int /*night*/, const NightOutcome&) {}
    virtual void playerEliminated(int /*day*/, int /*player*/, const std::vector<DeathCause>& /*causes*/) {}
    virtual void gameEnded(Winner, int /*days*/) {}
};

}  // namespace mafia
