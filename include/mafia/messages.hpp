#pragma once

// Сообщения между ведущим и игроками и публичный снимок партии.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mafia/roles.hpp"
#include "mafia/shared_ptr.hpp"

namespace mafia {

// Abstain — воздержаться на голосовании; так ходит только ИИ-агент.
enum class ActionType { None, ClaimInnocent, Accuse, Vote, Kill, Heal, Check, Shoot, Compare, Guard, Abstain };

// Bodyguard — ответный выстрел телохранителя по нападавшему.
enum class DeathCause { Execution, Mafia, Maniac, Commissar, Unknown, Bodyguard };

// Saved — доктор спас атакованного; публикуется только при открытых объявлениях.
// Abstain — игрок воздержался на голосовании (так ходит только ИИ-агент).
enum class EventType { Statement, Vote, Death, Saved, Abstain };

// Событие, которое слышали все игроки за столом.
struct PublicEvent {
    EventType type = EventType::Statement;
    int day = 0;
    int actor = 0;   // кто говорил или голосовал
    int target = 0;  // кого обвинили, за кого голосовали, кто погиб
    ActionType statement = ActionType::None;
    bool confident = false;
    std::string text;  // что именно сказал игрок (для реплик)
    std::vector<DeathCause> causes;  // при закрытых объявлениях ночью — Unknown
    std::optional<Side> side;
    std::optional<Role> role;  // только при открытых объявлениях
};

struct PlayerPublicInfo {
    int id = 0;
    bool alive = true;
    std::optional<Side> revealedSide;
    std::optional<Role> revealedRole;
};

// Неизменяемый снимок публичного состояния. Ведущий создаёт новый снимок
// на каждую фазу и раздаёт его игрокам через SharedPtr<const GameView>.
struct GameView {
    int day = 0;
    bool openAnnouncements = false;
    // Публичный состав партии (ведущий объявляет его в начале).
    int mafiaTotal = 0;  // вся команда мафии, включая ниндзя
    int ninjaTotal = 0;
    std::vector<PlayerPublicInfo> players;  // индекс = id - 1
    std::vector<PublicEvent> events;

    int playerCount() const noexcept { return static_cast<int>(players.size()); }

    bool isAlive(int id) const noexcept {
        return id >= 1 && id <= playerCount() && players[static_cast<std::size_t>(id - 1)].alive;
    }
};

enum class RequestType { Stop, Discussion, DayVote, Night, Info };

struct CheckResult {
    int target = 0;
    bool isMafia = false;
};

// Ответ журналисту: одинаково ли выглядят два игрока при проверке.
struct CompareResult {
    int first = 0;
    int second = 0;
    bool same = false;
};

// Запрос ведущего игроку. На Info и Stop игрок не отвечает.
struct Request {
    RequestType type = RequestType::Stop;
    std::uint64_t seq = 0;
    SharedPtr<const GameView> view;
    std::vector<int> targets;  // допустимые цели, уже с учётом правил
    std::optional<CheckResult> check;
    std::optional<CompareResult> comparison;
    // Сколько запросов к API ИИ-агент может сделать на этом ходу: первый и, если
    // ответ модели не прошёл проверку, повтор. Квоту раунда раздаёт ведущий.
    int aiCalls = 0;
};

struct Response {
    int player = 0;
    std::uint64_t seq = 0;
    ActionType action = ActionType::None;
    int target = 0;
    int secondTarget = 0;  // только у журналиста: второй сравниваемый игрок
    bool confident = false;
    std::string message;  // собственная реплика ИИ-агента; пусто — ведущий говорит шаблоном
    std::string reason;   // объяснение бота или рассуждение агента — только в полный лог
    bool valid = true;
    std::string error;
    bool endOfInput = false;
    int aiCalls = 0;  // сколько запросов к API сделал ИИ-агент — неиспользованные ведущий вернёт в квоту
};

}  // namespace mafia
