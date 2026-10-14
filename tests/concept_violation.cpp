// Этот файл НЕ должен компилироваться ни в одном из вариантов. CTest собирает его
// с MAFIA_VIOLATION = 1, 2, 3 и ждёт ошибку с упоминанием нужного концепта:
//   1 — класс роли без vote() передан в makeBot           → RoleBot;
//   2 — ночной ход для мирного жителя (playNightTurn)     → NightPlayer;
//   3 — мирный житель в списке ночных ролей ведущего      → NightRole.

#include <cstdint>

#include "mafia/bots.hpp"
#include "mafia/player_concepts.hpp"
#include "mafia/role_dispatch.hpp"

namespace {

class Silent final : public mafia::Player {
public:
    static constexpr mafia::Role kRole = mafia::Role::Civilian;
    Silent(int id, std::uint64_t) : Player(id, kRole) {}
    mafia::Response discuss(const mafia::Request&) { return {}; }

protected:
    mafia::Response handle(const mafia::Request&) override { return {}; }
};

}  // namespace

int main() {
#if MAFIA_VIOLATION == 1
    auto bot = mafia::makeBot<Silent>(1, 0);
    return bot ? 0 : 1;
#elif MAFIA_VIOLATION == 2
    auto civilian = mafia::makeBot<mafia::Civilian>(1, 0);
    const mafia::Response move = mafia::playNightTurn(*civilian, mafia::Request{});
    return move.valid ? 0 : 1;
#elif MAFIA_VIOLATION == 3
    using WrongList = mafia::RoleList<mafia::Doctor, mafia::Civilian>;
    return mafia::visitNightRole(WrongList{}, mafia::Role::Doctor, []<class R>() {}) ? 0 : 1;
#else
#error "задайте MAFIA_VIOLATION = 1, 2 или 3"
#endif
}
