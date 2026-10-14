#pragma once

// Разрешение ночи — чистая функция без нитей и ввода-вывода, её легко тестировать.

#include <map>
#include <optional>
#include <vector>

#include "mafia/messages.hpp"
#include "mafia/roles.hpp"

namespace mafia {

// Что знает ведущий в начале ночи. По нему класс роли определяет допустимые
// цели своего хода (статическая функция nightTargets, см. концепт NightRole).
struct NightContext {
    std::vector<int> alive;       // живые игроки по возрастанию номеров
    std::vector<Role> roles;      // индекс = id - 1
    std::optional<int> lastHeal;  // кого доктор лечил прошлой ночью

    std::vector<int> aliveExcept(int player) const;
    std::vector<int> aliveOutsideTeam(Team team) const;
};

// Ходы одной ночи, уже проверенные ведущим.
struct NightMoves {
    struct Move {
        int actor = 0;
        int target = 0;
    };
    struct Comparison {
        int actor = 0;
        int first = 0;
        int second = 0;
    };

    std::vector<Move> mafia;  // предложения мафии по возрастанию номеров; первый — босс
    std::optional<Move> maniac;
    std::optional<Move> commissar;
    bool commissarShoots = false;  // иначе — проверка
    std::optional<Move> doctor;
    std::optional<Move> bodyguard;
    std::optional<Comparison> journalist;
};

struct NightOutcome {
    std::optional<int> mafiaVictim;
    int mafiaExecutor = 0;  // мафиози, который стрелял (по нему отвечает телохранитель)

    // Все покушения ночи: цель → причины. Сначала мафия, затем остальные по номерам стрелявших.
    std::map<int, std::vector<DeathCause>> attacks;
    std::map<int, std::vector<DeathCause>> healed;   // спасены доктором
    std::map<int, std::vector<DeathCause>> guarded;  // прикрыты телохранителем
    std::optional<int> counterKilled;                // в кого выстрелил телохранитель
    std::vector<int> deaths;                         // по возрастанию номеров

    std::optional<CheckResult> check;
    std::optional<CompareResult> comparison;
};

// Правила:
//  * мафия убивает цель большинства предложений, при равенстве решает босс;
//  * лечение доктора спасает от всех выстрелов ночи, включая ответный выстрел телохранителя;
//  * телохранитель спасает подопечного от всех выстрелов; если стреляли мафия или маньяк,
//    он стреляет в одного из них — с наименьшим номером (от комиссара просто спасает);
//  * проверка комиссара и сравнение журналиста видят маньяка и ниндзя мирными.
// roles[id - 1] — роль игрока id.
NightOutcome resolveNight(const NightMoves& moves, const std::vector<Role>& roles);

}  // namespace mafia
