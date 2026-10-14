#include "mafia/game_events.hpp"

namespace mafia {

std::string_view winnerName(Winner winner) {
    switch (winner) {
        case Winner::Town:
            return "мирные жители";
        case Winner::Mafia:
            return "мафия";
        case Winner::Maniac:
            return "маньяк";
        case Winner::Draw:
            return "ничья";
        case Winner::Aborted:
            return "партия прервана";
    }
    return "?";
}

std::string_view causeName(DeathCause cause) {
    switch (cause) {
        case DeathCause::Execution:
            return "голосование";
        case DeathCause::Mafia:
            return "мафия";
        case DeathCause::Maniac:
            return "маньяк";
        case DeathCause::Commissar:
            return "комиссар";
        case DeathCause::Unknown:
            return "неизвестно";
        case DeathCause::Bodyguard:
            return "телохранитель";
    }
    return "?";
}

}  // namespace mafia
