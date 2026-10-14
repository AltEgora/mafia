cmake_minimum_required(VERSION 3.20)
project(Mafia LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# compile_commands.json в каталоге сборки — по нему IntelliSense (VS Code) и clangd
# видят те же флаги, пути и стандарт C++20, что и компилятор.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Тип сборки" FORCE)
endif()

# Например: cmake -B build-tsan -DMAFIA_SANITIZER=thread
set(MAFIA_SANITIZER "" CACHE STRING "Санитайзер: thread, address, undefined или пусто")
if(MAFIA_SANITIZER)
  add_compile_options(-fsanitize=${MAFIA_SANITIZER} -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${MAFIA_SANITIZER})
endif()

find_package(Threads REQUIRED)

add_library(mafia_core STATIC
  src/bots.cpp
  src/ai_agent.cpp
  src/config.cpp
  src/file_log.cpp
  src/game_events.cpp
  src/game_rules.cpp
  src/human_player.cpp
  src/json.cpp
  src/llm_curl.cpp
  src/llm_protocol.cpp
  src/logger.cpp
  src/moderator.cpp
  src/night.cpp
  src/player.cpp
  src/player_concepts.cpp
  src/player_runtime.cpp
  src/roles.cpp
  src/roles_config.cpp
  src/yaml.cpp
)
target_include_directories(mafia_core PUBLIC include)
target_link_libraries(mafia_core PUBLIC Threads::Threads)

# ИИ-агенты ходят в бесплатный OpenAI-совместимый API (OpenRouter) через libcurl. Без неё проект собирается так же,
# а агенты всегда играют как обычные боты.
option(MAFIA_WITH_CURL "Использовать libcurl для ИИ-агентов" ON)
set(MAFIA_HAVE_CURL 0)
if(MAFIA_WITH_CURL)
  find_package(CURL QUIET)
  if(CURL_FOUND)
    set(MAFIA_HAVE_CURL 1)
    target_link_libraries(mafia_core PRIVATE CURL::libcurl)
  endif()
endif()
target_compile_definitions(mafia_core PRIVATE MAFIA_HAVE_CURL=${MAFIA_HAVE_CURL})
message(STATUS "ИИ-агенты: libcurl ${MAFIA_HAVE_CURL} (1 — HTTPS-клиент, 0 — только fallback на ботов)")
target_compile_options(mafia_core PUBLIC
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow>)

# Назначенные инициализаторы с пропуском полей, у которых есть значения
# по умолчанию (Request{.type = ..., .view = ...}), используются намеренно.
# Об этом предупреждают -Wextra в clang >= 19 и GCC (-Wmissing-field-initializers);
# отключаем только эту категорию.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wno-missing-designated-field-initializers MAFIA_HAS_DESIGNATED_WARNING)
if(MAFIA_HAS_DESIGNATED_WARNING AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(mafia_core PUBLIC -Wno-missing-designated-field-initializers)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(mafia_core PUBLIC -Wno-missing-field-initializers)
endif()

add_executable(mafia src/main.cpp)
target_link_libraries(mafia PRIVATE mafia_core)

enable_testing()

add_executable(shared_ptr_tests tests/test_shared_ptr.cpp)
target_link_libraries(shared_ptr_tests PRIVATE mafia_core)
add_test(NAME shared_ptr_tests COMMAND shared_ptr_tests)

# Модульные тесты: YAML, конфигурация ролей, разрешение ночи, файловые логи, концепты.
foreach(test_name test_yaml test_roles_config test_night test_rules test_ranges test_file_log test_concepts
                  test_coro test_bots test_json test_ai_agent)
  add_executable(${test_name} tests/${test_name}.cpp)
  target_link_libraries(${test_name} PRIVATE mafia_core)
  target_compile_definitions(${test_name} PRIVATE MAFIA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# Неподходящие классы не должны компилироваться: ждём ошибку с упоминанием концепта.
#   1 — класс роли без vote() в makeBot; 2 — ночной ход мирного жителя;
#   3 — мирный житель в списке ночных ролей ведущего.
foreach(violation 1 2 3)
  add_executable(concept_violation_${violation} EXCLUDE_FROM_ALL tests/concept_violation.cpp)
  target_link_libraries(concept_violation_${violation} PRIVATE mafia_core)
  target_compile_definitions(concept_violation_${violation} PRIVATE MAFIA_VIOLATION=${violation})
endforeach()
set(VIOLATION_1_CONCEPT RoleBot)
set(VIOLATION_2_CONCEPT NightPlayer)
set(VIOLATION_3_CONCEPT NightRole)
set(VIOLATION_1_NAME concept_rejects_role_without_vote)
set(VIOLATION_2_NAME concept_rejects_night_turn_for_civilian)
set(VIOLATION_3_NAME concept_rejects_civilian_in_night_roles)
foreach(violation 1 2 3)
  add_test(NAME ${VIOLATION_${violation}_NAME}
    COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET=concept_violation_${violation}
            -DCONCEPT=${VIOLATION_${violation}_CONCEPT} -DCONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_compile_failure.cmake)
endforeach()

# Дымовые тесты: полные партии ботов разного размера в обоих режимах объявлений.
set(EXTENDED_ROLES ${CMAKE_CURRENT_SOURCE_DIR}/configs/extended.yaml)
foreach(players 5 6 9 16 40)
  add_test(NAME game_closed_${players} COMMAND mafia --players ${players} --seed ${players} --full-log --no-pauses)
  add_test(NAME game_open_${players} COMMAND mafia --players ${players} --seed ${players} --open-announcements --no-pauses)
  add_test(NAME game_extended_closed_${players}
    COMMAND mafia --players ${players} --seed ${players} --full-log --no-pauses --roles-config ${EXTENDED_ROLES})
  add_test(NAME game_extended_open_${players}
    COMMAND mafia --players ${players} --seed ${players} --open-announcements --no-pauses --roles-config ${EXTENDED_ROLES})
endforeach()

# Режим корутин: те же партии, вывод должен совпасть с режимом нитей побайтно.
foreach(players 5 9 16)
  add_test(NAME game_coroutines_${players}
    COMMAND mafia --players ${players} --seed ${players} --full-log --no-pauses --coroutines)
  add_test(NAME game_coroutines_extended_${players}
    COMMAND mafia --players ${players} --seed ${players} --open-announcements --no-pauses --coroutines
            --roles-config ${EXTENDED_ROLES})
endforeach()

# --mafia-k: при N = 10 и k = 4 мафии floor(10 / 4) = 2; k < 3 — ошибка.
add_test(NAME game_mafia_k COMMAND mafia --players 10 --seed 1 --no-pauses --mafia-k 4)
set_tests_properties(game_mafia_k PROPERTIES PASS_REGULAR_EXPRESSION "Игроков: 10\\. Мафия: 2,")
add_test(NAME rejects_mafia_k_below_3 COMMAND mafia --players 10 --mafia-k 2)
set_tests_properties(rejects_mafia_k_below_3 PROPERTIES WILL_FAIL TRUE)

# ИИ-агенты в ctest всегда без ключа: тесты не ходят в сеть, агенты играют ботами.
add_test(NAME game_ai_agents_flag COMMAND mafia --players 9 --seed 3 --no-pauses --full-log --ai-agents 3)
add_test(NAME game_ai_agents_yaml
  COMMAND mafia --players 10 --seed 4 --no-pauses --coroutines --roles-config ${CMAKE_CURRENT_SOURCE_DIR}/configs/ai_agents.yaml)
add_test(NAME game_ai_agents_more_than_players COMMAND mafia --players 6 --seed 5 --no-pauses --ai-agents 50)
set_tests_properties(game_ai_agents_flag game_ai_agents_yaml game_ai_agents_more_than_players PROPERTIES
  ENVIRONMENT "OPENROUTER_API_KEY=;MAFIA_LLM_API_KEY=;MAFIA_LLM_BASE_URL=;MAFIA_LLM_MODEL="
  PASS_REGULAR_EXPRESSION "ИИ-агенты: Игрок")
add_test(NAME rejects_negative_ai_agents COMMAND mafia --players 9 --ai-agents -1)
set_tests_properties(rejects_negative_ai_agents PROPERTIES WILL_FAIL TRUE)

add_test(NAME rejects_four_players COMMAND mafia --players 4)
set_tests_properties(rejects_four_players PROPERTIES WILL_FAIL TRUE)

# При пяти игроках расширенному набору не хватает мест — роль пропускается с сообщением.
add_test(NAME extended_roles_skip_message COMMAND mafia --players 5 --seed 1 --no-pauses --roles-config ${EXTENDED_ROLES})
set_tests_properties(extended_roles_skip_message PROPERTIES
  PASS_REGULAR_EXPRESSION "Роль «Журналист» пропущена: при 5 игроках для неё не хватило места")

# Ошибки в файле ролей сообщаются с именем файла и номером строки.
add_test(NAME roles_config_bad_indent
  COMMAND mafia --players 7 --roles-config ${CMAKE_CURRENT_SOURCE_DIR}/tests/configs/bad_indent.yaml)
set_tests_properties(roles_config_bad_indent PROPERTIES PASS_REGULAR_EXPRESSION "bad_indent.yaml:3: ")
add_test(NAME roles_config_unknown_role
  COMMAND mafia --players 7 --roles-config ${CMAKE_CURRENT_SOURCE_DIR}/tests/configs/unknown_role.yaml)
set_tests_properties(roles_config_unknown_role PROPERTIES
  PASS_REGULAR_EXPRESSION "unknown_role.yaml:3: неизвестная роль «wizard»")

if(UNIX)
  add_test(NAME game_interactive
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/interactive_smoke.sh $<TARGET_FILE:mafia>)
  add_test(NAME game_interactive_extended
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/interactive_smoke.sh $<TARGET_FILE:mafia>
            --players 9 --roles-config ${EXTENDED_ROLES})
  add_test(NAME game_interactive_coroutines
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/interactive_smoke.sh $<TARGET_FILE:mafia> --coroutines)
  add_test(NAME game_interactive_coroutines_extended
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/interactive_smoke.sh $<TARGET_FILE:mafia>
            --players 9 --roles-config ${EXTENDED_ROLES} --coroutines)
  add_test(NAME coroutines_match_threads
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/modes_match.sh $<TARGET_FILE:mafia> ${EXTENDED_ROLES})
  add_test(NAME log_files_written
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/log_files_smoke.sh $<TARGET_FILE:mafia>
            ${CMAKE_CURRENT_BINARY_DIR}/test-logs ${CMAKE_CURRENT_SOURCE_DIR}/configs)
  add_test(NAME log_failure_keeps_playing
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/log_failure_smoke.sh $<TARGET_FILE:mafia>
            ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
endif()
