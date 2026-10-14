// Проверки концептов для всех классов игроков. Файл ничего не делает во время
// выполнения: если какая-то роль потеряет нужный метод или правило хода,
// сборка упадёт здесь.

#include "mafia/player_concepts.hpp"

#include "mafia/bots.hpp"
#include "mafia/human_player.hpp"
#include "mafia/role_dispatch.hpp"

namespace mafia {

// Все классы ролей ботов создаются через makeBot.
static_assert(RoleBot<Civilian> && RoleBot<Mafioso> && RoleBot<Ninja> && RoleBot<Commissar> &&
              RoleBot<Doctor> && RoleBot<Maniac> && RoleBot<Journalist> && RoleBot<Bodyguard>);

// Ночные роли знают свои правила хода.
static_assert(NightRole<Mafioso> && NightRole<Ninja> && NightRole<Commissar> && NightRole<Doctor> &&
              NightRole<Maniac> && NightRole<Journalist> && NightRole<Bodyguard>);

// Мирный житель — участник дня, но ночного хода у него нет.
static_assert(DayPlayer<Civilian> && RoleClass<Civilian>);
static_assert(!NightPlayer<Civilian> && !NightRole<Civilian>);

static_assert(Civilian::kRole == Role::Civilian && Mafioso::kRole == Role::Mafia && Ninja::kRole == Role::Ninja &&
              Commissar::kRole == Role::Commissar && Doctor::kRole == Role::Doctor &&
              Maniac::kRole == Role::Maniac && Journalist::kRole == Role::Journalist &&
              Bodyguard::kRole == Role::Bodyguard);

// Человек может получить любую роль: ходит и днём, и ночью, но класса роли у него нет.
static_assert(NightPlayer<HumanPlayer> && !RoleClass<HumanPlayer> && !RoleBot<HumanPlayer>);

// Базовые классы — не игроки: Player, BotPlayer и NightBot абстрактны.
static_assert(!DayPlayer<Player> && !DayPlayer<BotPlayer> && !DayPlayer<NightBot>);

}  // namespace mafia
