# Архитектура проекта «Мафия»: карта файлов

Документ для разбора кода: что делает каждый файл, как файлы связаны и в каком
порядке их читать. Правила игры, флаги и примеры запуска — в [README](../README.md),
ИИ-агент подробно — в [AI_TRACK.md](AI_TRACK.md).

Объём: 25 заголовков (`include/mafia/`, ≈2,4 тыс. строк), 19 исходников
(`src/`, ≈4,8 тыс. строк), 21 файл тестов и вспомогательных скриптов
(`tests/`, ≈3,1 тыс. строк).

---

## 1. Как читать: рекомендуемый порядок

1. **Вход и настройка:** `src/main.cpp` → `config.hpp/.cpp` → `roles_config.hpp/.cpp`.
2. **Данные партии:** `roles.hpp` → `messages.hpp` (запросы, ответы, снимок партии).
3. **Ведущий:** `moderator.hpp` → `src/moderator.cpp` (`run()` — весь ход партии).
4. **Игроки:** `player.hpp` → `bots.hpp/.cpp` → `human_player.hpp/.cpp`.
5. **Концепты:** `player_concepts.hpp` → `role_dispatch.hpp` → `src/player_concepts.cpp`.
6. **Параллельность:** `shared_ptr.hpp` → `channel.hpp` → `coro.hpp` → `player_runtime.hpp/.cpp`.
7. **Правила как чистые функции:** `night.hpp/.cpp`, `game_rules.hpp/.cpp`, `ranges_util.hpp`.
8. **Вывод:** `logger.hpp/.cpp`, `game_events.hpp/.cpp`, `file_log.hpp/.cpp`.
9. **ИИ-агент:** `ai_agent.hpp/.cpp` → `llm_client.hpp` → `llm_protocol.cpp` → `llm_curl.cpp` → `json.hpp/.cpp`.
10. **Тесты:** `tests/test_framework.hpp`, затем тест нужного модуля (карта в разделе 9).

---

## 2. Дерево проекта

```text
mafia/
├── CMakeLists.txt            сборка: библиотека mafia_core, программа mafia, 12 тестовых
│                             программ, ctest-партии, санитайзеры, libcurl (необязательна)
├── README.md                 правила, флаги, запуск, обзор модулей, тесты
├── .gitignore                build*/, logs/, *.jsonl (трассировки API)
├── .vscode/settings.json     IntelliSense через clangd и build/compile_commands.json
├── configs/                  наборы ролей (YAML)
│   ├── classic.yaml          классика: мафия auto, комиссар, доктор, маньяк
│   ├── extended.yaml         + ниндзя, журналист, телохранитель
│   └── ai_agents.yaml        расширенный набор + раздел ai_agents (count, model,
│                             10 характеристик агентов)
├── docs/
│   ├── AI_TRACK.md           трек с ИИ: модель, что делегировано, пример промпта и ответа
│   └── ARCHITECTURE.md       этот документ
├── include/mafia/            заголовки (интерфейсы, шаблоны, концепты)
│   ├── shared_ptr.hpp        SharedPtr<T>, MakeShared — свой умный указатель
│   ├── channel.hpp           Channel<T> — блокирующая очередь для нитей
│   ├── coro.hpp              Task<T>, Scheduler, CoChannel<T>, AsyncInput — корутины
│   ├── random.hpp            Rng, mixSeed — воспроизводимая случайность
│   ├── roles.hpp             Role, Team, Side, имена ролей, число мафии
│   ├── messages.hpp          Request, Response, PublicEvent, GameView
│   ├── config.hpp            GameConfig, parseArgs, usage
│   ├── yaml.hpp              свой парсер подмножества YAML
│   ├── roles_config.hpp      RoleConfig, AiAgentsConfig, buildRoles
│   ├── player.hpp            Player — базовый класс игрока (нить или корутина)
│   ├── player_concepts.hpp   концепты DayPlayer/NightPlayer/RoleClass/NightRole/RoleBot,
│   │                         playTurn, makeBot
│   ├── bots.hpp              боты всех ролей и их ночные правила
│   ├── human_player.hpp      HumanPlayer — игрок-человек
│   ├── role_dispatch.hpp     списки классов ролей, visitRoleBot, visitNightRole,
│   │                         collectNightMoves
│   ├── player_runtime.hpp    PlayerRuntime: ThreadRuntime и CoroutineRuntime
│   ├── night.hpp             NightContext, NightMoves, NightOutcome, resolveNight
│   ├── game_rules.hpp        countTeams, decideWinner, tallyVotes, voteLeaders
│   ├── ranges_util.hpp       toVector, contains, allExcept
│   ├── logger.hpp            Logger — единственная точка вывода в консоль
│   ├── game_events.hpp       GameEventSink — подписчик на события партии
│   ├── file_log.hpp          FileLog — логи раундов и итоговая статистика в файлы
│   ├── moderator.hpp         Moderator, GameState — ведущий
│   ├── json.hpp              json::Value, parse, dump — для API модели
│   ├── llm_client.hpp        LlmClient, LlmOptions, протокол Chat Completions
│   └── ai_agent.hpp          AiAgent<Bot>, AgentBrain — ИИ-агент
├── src/                      реализации (по одному .cpp на заголовок, кроме шаблонных)
│   ├── main.cpp              точка входа
│   ├── config.cpp            разбор аргументов командной строки
│   ├── yaml.cpp              парсер YAML с номерами строк в ошибках
│   ├── roles_config.cpp      чтение набора ролей и ai_agents, раскладка ролей на N мест
│   ├── roles.cpp             имена ролей, лагеря, стороны, mafiaCount
│   ├── player.cpp            цикл игрока: нить run() и корутина play()
│   ├── player_runtime.cpp    запуск и остановка игроков в обоих режимах
│   ├── bots.cpp              решения ботов: подозрения, обвинения, голоса, ночные ходы
│   ├── human_player.cpp      разбор ввода человека
│   ├── player_concepts.cpp   только static_assert: все роли удовлетворяют концептам
│   ├── night.cpp             разрешение ночи (чистая функция)
│   ├── game_rules.cpp        итоги голосования (чистые функции)
│   ├── logger.cpp            вывод в консоль, отложенные «секреты»
│   ├── game_events.cpp       имена победителя и причин смерти
│   ├── file_log.cpp          запись round_NN.txt и summary.txt
│   ├── moderator.cpp         ход партии: день, голосование, ночь, конец
│   ├── json.cpp              разбор и сериализация JSON
│   ├── llm_protocol.cpp      тело запроса и разбор ответа Chat Completions (без сети)
│   ├── llm_curl.cpp          HTTPS через libcurl, трассировка; без libcurl — заглушка
│   └── ai_agent.cpp          промпт, проверка ответа, повтор, лимиты агента
└── tests/
    ├── test_framework.hpp    мини-фреймворк: TEST, CHECK, CHECK_EQ, CHECK_THROWS_WITH
    ├── test_*.cpp            модульные тесты (карта — раздел 9)
    ├── concept_violation.cpp классы, которые НЕ должны компилироваться (3 варианта)
    ├── expect_compile_failure.cmake  ждёт ошибку компиляции с именем концепта
    ├── interactive_smoke.sh  партия с человеком по сценарию ввода
    ├── modes_match.sh        вывод нитей и корутин совпадает побайтно
    ├── log_files_smoke.sh    файлы логов создаются
    ├── log_failure_smoke.sh  партия идёт, даже если каталог логов создать нельзя
    └── configs/              битые YAML для тестов ошибок (отступ, неизвестная роль)
```

---

## 3. Слои и зависимости

Стрелка «A → B» значит «A использует B». Зависимости идут сверху вниз, циклов нет.

```text
                              main.cpp
                     ┌───────────┼──────────────┐
                 config      roles_config ── yaml
                                 │
                             Moderator ───────────────┬──────────────┬──────────────┐
             ┌──────────────┬────┼──────────┬─────────┤              │              │
          Logger        FileLog  │      night /     role_dispatch  PlayerRuntime  AiAgent<Bot>
                     (GameEventSink) game_rules        │          ┌────┴─────┐     │
                                 │                     │       Thread   Coroutine  ├── AgentBrain
                                 │                     │       Runtime  Runtime    │     │
                                 │                     │          │        │       │   LlmClient
                                 │                  bots ── player_concepts        │   ├ llm_protocol
                                 │                     │                           │   └ llm_curl ── libcurl
                                 │                  Player ── Channel / coro       │        │
                                 │                     │                           │      json
                              messages ── roles     SharedPtr                      └── bots (вложенный бот)
```

Ключевые правила архитектуры:

- **Печатает только ведущий.** Игроки возвращают `Response`, весь вывод идёт через
  `Logger` и подписчиков `GameEventSink` в главной нити.
- **Игроки не видят `GameState`.** Им приходит неизменяемый снимок
  `SharedPtr<const GameView>` — общий для всех, без гонок.
- **Общие структуры — только `SharedPtr`.** `std::shared_ptr` и `std::unique_ptr`
  в проекте не используются.
- **Правила — чистые функции** (`resolveNight`, `countTeams`, `decideWinner`,
  `tallyVotes`): их тестируют без нитей и ввода-вывода.
- **Случайность воспроизводима.** У ведущего и каждого игрока свой `Rng` из сида
  партии (`mixSeed`), поэтому `--seed S` повторяет партию в обоих режимах.

---

## 4. Как идёт партия (`Moderator::run`)

```text
main()
 ├─ parseArgs → GameConfig;  loadRoleConfig → RoleConfig (если --roles-config)
 └─ Moderator(config, roles, cin, cout).run()
     ├─ setup()            buildRoles → роли по местам; chooseAgents → кто ИИ-агент;
     │                     создание игроков (visitRoleBot → makeBot / makeAgent / HumanPlayer);
     │                     команда мафии (setTeam); запуск PlayerRuntime (нити или корутины)
     ├─ announceStart()    состав, режим, сид, агенты; FileLog::gameStarted
     ├─ День 1: startDay() → discussion()             (без голосования)
     └─ цикл:
         ├─ night()        запросы Night ночным ролям → collectNightMoves → resolveNight
         │                 → смерти, спасения, проверки (Info комиссару/журналисту)
         ├─ checkWinner()  countTeams → decideWinner
         ├─ discussion()   запросы Discussion всем → реплики по порядку номеров
         ├─ voting()       запросы DayVote → tallyVotes → voteLeaders → монетка при равенстве
         │                 → eliminate(); все воздержались — никто не выбывает
         ├─ checkWinner()
         └─ предел 100 дней → ничья
     finish() → итоги, FileLog::gameEnded;  shutdown() → Stop всем игрокам
```

Каждая фаза устроена одинаково: ведущий собирает `Batch` запросов, вызывает
`ask()`, который рассылает их через `PlayerRuntime` и собирает ответы в
`std::map<int, Response>` (упорядочено по номерам). Неверный ответ
(`isValid`) заменяется ходом ведущего (`fallback`). Затем ведущий обновляет
`GameState`, печатает итоги и вызывает `emit(...)` для подписчиков.

---

## 5. Один ход игрока: запрос → ответ

```text
Moderator::ask(batch)
  └─ runtime.send(id, Request{type, view, targets, aiCalls})
        │
        ├─ ThreadRuntime:    Channel<Request> → нить игрока Player::run()
        └─ CoroutineRuntime: CoChannel<Request> → корутина Player::play() (в нити ведущего)
                                   │
                         Player::handle(request)           (виртуальный, у каждого класса)
                                   │
                         playTurn(*this, request)          (выбор перегрузки по концептам)
                          ├─ DayPlayer:   Discussion → discuss(),  DayVote → vote()
                          └─ NightPlayer: + Night → act()
                                   │
                         Response{action, target, message, reason, aiCalls}
        ┌──────────────────────────┘
  runtime.receive()  →  answers[id]
```

- **Режим нитей:** у каждого игрока своя `std::thread` и две блокирующие очереди
  `Channel` (запросы и ответы).
- **Режим корутин (`--coroutines`):** все игроки — корутины `Task<>` в нити ведущего;
  `Scheduler` возобновляет их по очереди. Человек ждёт строку ввода через
  `AsyncInput`, не задерживая ботов. Вывод обоих режимов совпадает побайтно.
- `Info` (результат проверки) и `Stop` — запросы без ответа.

---

## 6. Файлы подробно

### 6.1. Основа: память, очереди, корутины, случайность

| Файл | Что внутри | Кто использует | Тесты |
|---|---|---|---|
| `shared_ptr.hpp` | `SharedPtr<T>`: атомарный счётчик в управляющем блоке (`ControlBlockBase`); блоки для `new` с удалителем и для `MakeShared` (одна аллокация); копирование, перемещение, преобразование `Derived → Base`, `reset`, `swap`, сравнения. Без `weak_ptr`. | все модули | `test_shared_ptr` |
| `channel.hpp` | `Channel<T>`: очередь на `mutex` + `condition_variable`; `push`, блокирующий `pop`. | `player.hpp`, `ThreadRuntime` | косвенно — партии |
| `coro.hpp` | `Task<T>` со своим `promise_type` (ленивый старт, симметричная передача управления, исключения); `Scheduler` — однопоточная очередь готовых корутин; `CoChannel<T>` — канал, на котором корутина засыпает; `AsyncInput` — строка ввода человека. | `player.hpp`, `CoroutineRuntime`, `HumanPlayer` | `test_coro` |
| `random.hpp` | `Rng` (обёртка над `mt19937_64`: `uniform`, `uniformInt`, `chance`, `pick`, `shuffle`); `mixSeed` (splitmix64) — независимые сиды для ведущего, игроков, выбора агентов. | ведущий, боты | косвенно |

### 6.2. Данные партии и настройки

| Файл | Что внутри | Кто использует | Тесты |
|---|---|---|---|
| `roles.hpp` / `roles.cpp` | `Role` (8 ролей), `Team` (мирные, мафия, маньяк), `Side` (как видят проверки); `roleName`, `teamName`, `teamOf`, `checkSide` (маньяк и ниндзя «мирные» при проверке), `announcedSide`, `hasNightAction`, `mafiaCount(N, k)`. | везде | `test_roles_config`, `test_night` |
| `messages.hpp` | `Request` (тип, снимок, допустимые цели, `aiCalls`), `Response` (действие, цель, реплика агента, причина, `aiCalls`), `PublicEvent` (реплика, голос, воздержание, смерть, спасение), `GameView` — неизменяемый снимок партии, `CheckResult`, `CompareResult`. | все игроки и ведущий | везде |
| `config.hpp` / `config.cpp` | `GameConfig` (игроки, режимы, `--mafia-k`, `--ai-agents`, сид, файлы); `parseArgs` с понятными ошибками; `usage` — текст `--help`. | `main.cpp`, ведущий | `test_roles_config`, ctest `rejects_*` |
| `yaml.hpp` / `yaml.cpp` | Парсер подмножества YAML: словари, списки, скаляры в кавычках, `[a, b]`, комментарии. Неподдерживаемое (табы, якоря, `|`, `{}`) — ошибка с номером строки. | `roles_config.cpp` | `test_yaml` |
| `roles_config.hpp` / `roles_config.cpp` | `RoleConfig` (особые роли с приоритетом, `mafia: auto`), `AiAgentsConfig` (`count`, `model`, `characteristics`); `parseRoleConfig`/`loadRoleConfig` с ошибками «файл:строка»; `buildRoles` раскладывает роли на N мест, пропуская не поместившиеся. | `main.cpp`, ведущий | `test_roles_config` |

### 6.3. Игроки и концепты

| Файл | Что внутри | Кто использует | Тесты |
|---|---|---|---|
| `player.hpp` / `player.cpp` | `Player`: номер, роль; `start`/`join` (нить с циклом `run()`), `play()` (тот же цикл корутиной); виртуальные `handle` и `onInfo`; `respondAsync` для корутин; `deliverInfo` для ИИ-агента. | все классы игроков | `test_coro`, партии |
| `player_concepts.hpp` | Концепты: `DayPlayer` (discuss + vote), `NightPlayer` (+ act; мирный житель — нет), `RoleClass` (`kRole`), `NightRole` (+ статические правила хода: `nightTargets`, `isValidNightMove`, `fallbackNightMove`, `recordNightMove`), `RoleBot` (конструктор из номера и сида). Шаблоны `playTurn` (перегрузки по концептам), `playDayTurn`, `playNightTurn`, `makeBot`. | боты, человек, агент, ведущий | `test_concepts`, `concept_violation` |
| `bots.hpp` / `bots.cpp` | `BotPlayer` — модель подозрений (обвинения, голоса, смерти, доверие); `Civilian`; `NightBot` → `Mafioso` (команда, угрозы, босс) → `Ninja`; `Commissar` (проверка или выстрел), `Doctor`, `Maniac`, `Journalist` (сравнение двоих), `Bodyguard`. У ночных классов — статические правила хода. | ведущий, ИИ-агент (как вложенный бот) | `test_bots`, `test_concepts` |
| `human_player.hpp` / `human_player.cpp` | `HumanPlayer`: читает строку (в нити — из потока, в корутине — из `AsyncInput`) и разбирает её под текущий запрос; подсказки печатает ведущий. | ведущий (`--interactive`) | `interactive_smoke.sh`, `test_coro` |
| `role_dispatch.hpp` | `RoleList`, `AllRoles`, `NightRoles`; `visitRoleBot` — по значению `Role` вызывает шаблон с классом роли; `visitNightRole`; `collectNightMove(s)` — сбор ночных ходов только для `NightRole`; `static_assert`, что каждой роли соответствует ровно один класс. | ведущий | `test_concepts` |
| `src/player_concepts.cpp` | Только `static_assert`: все роли удовлетворяют нужным концептам, а мирный житель — не ночной. Сборка упадёт, если роль потеряет метод. | — | (сам и есть проверка) |
| `player_runtime.hpp` / `player_runtime.cpp` | Интерфейс `PlayerRuntime` (`start`, `send`, `receive`, `stop`); `ThreadRuntime` — нити и `Channel`; `CoroutineRuntime` — корутины, `Scheduler`, ввод человека только когда больше некого продвигать. | ведущий | `test_coro`, `modes_match.sh` |

### 6.4. Правила (чистые функции)

| Файл | Что внутри | Тесты |
|---|---|---|
| `night.hpp` / `night.cpp` | `NightContext` (живые, роли, прошлое лечение) → цели ночных ролей; `NightMoves` — проверенные ходы; `resolveNight`: решение мафии (большинство, при равенстве — босс), выстрелы комиссара и маньяка, лечение доктора, телохранитель с ответным выстрелом, проверка и сравнение. | `test_night` |
| `game_rules.hpp` / `game_rules.cpp` | `Winner`; `countTeams` (один проход, любой `input_range`); `decideWinner`; `tallyVotes` (по убыванию голосов, воздержавшиеся не считаются); `voteLeaders`. | `test_rules`, `test_ai_agent` |
| `ranges_util.hpp` | `toVector` (замена `std::ranges::to` из C++23), `contains` (с проекцией, один проход), `allExcept`. | `test_ranges` |

### 6.5. Вывод: консоль и файлы

| Файл | Что внутри | Тесты |
|---|---|---|
| `logger.hpp` / `logger.cpp` | `Logger`: `announce` (всем), `secret` (только `--full-log`; в интерактиве — в конце партии), `personal` (человеку), `inputPrompt`. | партии ctest |
| `game_events.hpp` / `game_events.cpp` | `GameEventSink` — пустые виртуальные обработчики событий (`gameStarted`, `statement`, `voteCast`, `nightResolved`, `playerEliminated`, `gameEnded`…); `GameSetupInfo`; имена победителя и причин смерти. | `test_file_log` |
| `file_log.hpp` / `file_log.cpp` | `FileLog : GameEventSink`: `round_NN.txt` (день и ночь, мотивы, реплики агентов), `summary.txt` (итог, статистика по игрокам, ИИ-агенты). Ошибка записи — одно предупреждение, партия идёт дальше. | `test_file_log`, `log_*_smoke.sh` |

### 6.6. Ведущий

| Файл | Что внутри |
|---|---|
| `moderator.hpp` | `GameState` (роли, живые, день, история, прошлое лечение); `Moderator`: публичный `run()`, внутри — фазы, `ask`, `isValid`/`fallback`, `eliminate`, `checkWinner`, `snapshot`, квота ИИ (`grantAiCalls`/`returnAiCalls`), `emit` для подписчиков. |
| `src/moderator.cpp` | Самый большой файл (≈900 строк), читать по разделам: **подготовка** (`setup`, `chooseAgents`, `announceStart`), **квота ИИ** (10 запросов за раунд: сначала первые запросы агентам, затем повторы; неиспользованное возвращается), **день** (`discussion`, `voting` с `abstain`), **ночь** (`night`: цели по `NightRole`, `collectNightMoves`, `resolveNight`, объявления), **конец** (`finish`, `shutdown`), **обмен с игроками** (`ask`: рассылка, приём с проверкой `seq`, замена неверных ответов), **помощники** (`eliminate`, `checkWinner`, `snapshot`, имена). |

### 6.7. ИИ-агент и API модели

| Файл | Что внутри | Тесты |
|---|---|---|
| `json.hpp` / `json.cpp` | `json::Value` (`variant` из null, bool, number, string, массива, объекта с порядком ключей); `parse` (escape, `\u`, суррогатные пары, глубина ≤ 64, ошибка со смещением); `dump`. | `test_json` |
| `llm_client.hpp` | `LlmRequest` (system, prompt, повтор: `previousReply` + `correction`), `LlmReply` (ok, text, error, ответившая `model`); интерфейс `LlmClient` (мок в тестах); `LlmOptions` (модель, запасные модели, ключ, адрес, таймаут, `reasoningEffort`, трассировка); модель по умолчанию и `answeredByFallback`. | `test_ai_agent` |
| `src/llm_protocol.cpp` | Без сети: `buildChatRequest` (messages, `response_format: json_object`, `models` и `reasoning` только для OpenRouter); `parseChatResponse` (`choices[0]`, `<think>`, finish_reason, ошибки HTTP и с кодом 200); `llmOptionsFromEnvironment` (переменные `OPENROUTER_API_KEY`, `MAFIA_LLM_*`). | `test_ai_agent` |
| `src/llm_curl.cpp` | `CurlChatClient` — POST через libcurl (свой handle на запрос, таймауты, `NOSIGNAL`), трассировка в `MAFIA_LLM_TRACE` (без ключа); `UnavailableClient` — нет ключа или собрано без libcurl. | заглушка API вручную |
| `ai_agent.hpp` | `AgentIdentity`; `agentSystemPrompt`; `buildAgentPrompt`; `parseAgentReply` (проверки); `namedPlayers`; `AgentBrain` (запросы, повтор, лимит 10 за раунд, личные заметки); шаблон `AiAgent<Bot>` — день решает модель, ночь — вложенный `Bot`; `makeAgent`. | `test_ai_agent` |
| `src/ai_agent.cpp` | Промпт только из видимого игроку; проверки ответа: JSON, действие по этапу, живая допустимая цель, только русский, в реплике назван только `target`, `abstain` без цели; `AgentBrain::decide` — запрос, один повтор с пояснением ошибки, fallback на бота с причиной; в причине — какая модель ответила. | `test_ai_agent` |

Путь одного решения агента:

```text
Moderator.voting() ─ Request{DayVote, view, targets, aiCalls=1..2}
  → AiAgent<Bot>::vote → decideOr
      → AgentBrain::decide
          ├─ buildAgentPrompt (JSON: роль, характеристика, живые, история, личное)
          ├─ LlmClient::complete ─ buildChatRequest → libcurl POST → parseChatResponse
          ├─ parseAgentReply ── ок → Response (реплика агента, «ИИ (модель …): …»)
          │                   └─ невалидно → повтор с пояснением (если есть квота)
          └─ ошибка/второй плохой ответ → nullopt
      → nullopt? → bot_.vote(request) + «ИИ не ответил (…), ход бота: …»
  ← Response{..., aiCalls = сколько запросов потрачено}
Moderator.returnAiCalls — неиспользованное назад в квоту раунда
```

---

## 7. Иерархия игроков

```text
Player (player.hpp)                       handle(), onInfo(), run()/play()
├── HumanPlayer                           discuss/vote/act из строки ввода
├── AiAgent<Bot>                          день — модель, ночь — вложенный Bot
└── BotPlayer                             модель подозрений, discuss(), vote()
    ├── Civilian                          ночью спит → не NightPlayer
    └── NightBot                          + act()
        ├── Mafioso ── Ninja              команда мафии, босс; ниндзя «мирный» на проверке
        ├── Commissar                     проверка или выстрел
        ├── Doctor                        лечение (одного игрока — не две ночи подряд)
        ├── Maniac                        убийство, играет сам за себя
        ├── Journalist                    сравнение двух игроков
        └── Bodyguard                     охрана с ответным выстрелом
```

Как концепты проверяют иерархию:

| Концепт | Требует | Где ограничивает |
|---|---|---|
| `DayPlayer` | наследник `Player`, не абстрактный, `discuss` и `vote` | `playTurn`, `playDayTurn` |
| `NightPlayer` | `DayPlayer` + `act` | `playNightTurn`, ночная перегрузка `playTurn`, `AiAgent::act` |
| `RoleClass` | `DayPlayer` + `T::kRole` | списки ролей |
| `NightRole` | `RoleClass` + `NightPlayer` + статические правила хода | `visitNightRole`, `collectNightMove` |
| `RoleBot` | `RoleClass` + конструктор `(int, uint64_t)` | `makeBot`, `makeAgent`, `visitRoleBot`, `AiAgent<Bot>` |

---

## 8. Конфигурации и данные

| Файл | Для чего |
|---|---|
| `configs/classic.yaml` | то же, что без `--roles-config` (проверяется тестом) |
| `configs/extended.yaml` | все 7 особых ролей; при малом N лишние роли пропускаются с сообщением |
| `configs/ai_agents.yaml` | расширенные роли + `ai_agents`: `count: 3`, (необязательная) `model`, 10 характеристик — каждому агенту случайная, в партии разные |
| `tests/configs/*.yaml` | намеренно битые файлы для проверки ошибок с номером строки |
| `build/compile_commands.json` | флаги сборки для clangd/IntelliSense (создаётся CMake) |
| `trace.jsonl` (если есть) | трассировка запросов к API (`MAFIA_LLM_TRACE`), в git не попадает |

---

## 9. Карта тестов

| Тест | Что проверяет | Основной модуль |
|---|---|---|
| `test_shared_ptr` | конструкторы, копирование, перемещение, `Derived → Base`, удалители, `MakeShared`, счётчик под нагрузкой из нитей | `shared_ptr.hpp` |
| `test_yaml` | словари, списки, кавычки, комментарии, ошибки с номерами строк | `yaml` |
| `test_roles_config` | наборы ролей, `mafia: auto` и `--mafia-k`, пропуск ролей, раздел `ai_agents`, аргументы командной строки | `roles_config`, `config` |
| `test_night` | решение мафии и босс, доктор, телохранитель, проверка и сравнение | `night` |
| `test_rules` | `countTeams` (в том числе однопроходный диапазон), `decideWinner`, `tallyVotes`, `voteLeaders` | `game_rules` |
| `test_ranges` | `toVector`, `contains`, `allExcept` на разных диапазонах | `ranges_util` |
| `test_file_log` | файлы раундов, итоговая статистика, отказ записи | `file_log` |
| `test_concepts` | концепты на правильных и неправильных классах, `playTurn`, диспетчеризация ролей | `player_concepts`, `role_dispatch` |
| `test_coro` | `Task`, вложенный `co_await`, исключения, каналы, асинхронный ввод, корутинный режим игроков | `coro`, `player_runtime` |
| `test_bots` | журналист в закрытом и открытом режиме: ниндзя на сравнении выглядит мирным, раскрытые роли | `bots` |
| `test_json` | разбор и сериализация JSON | `json` |
| `test_ai_agent` | всё про агента на моке: ответы, ошибки, повтор, лимиты, язык, `abstain`, модель в логе, промпт, протокол API, характеристики, полные партии | `ai_agent`, `llm_*`, ведущий |
| `concept_violation_*` | классы-нарушители не компилируются (ошибка упоминает концепт) | `player_concepts` |
| `game_*` (ctest) | полные партии разных размеров, наборов и режимов; агенты без ключа | вся программа |
| `*.sh` (ctest) | интерактив, совпадение нитей и корутин, файлы логов | вся программа |

---

## 10. Где искать: вопрос → файл

| Вопрос | Файл и место |
|---|---|
| Как разбираются флаги? | `src/config.cpp` → `parseArgs` |
| Сколько мафии и кто какую роль получил? | `roles.cpp` → `mafiaCount`; `roles_config.cpp` → `buildRoles`; `moderator.cpp` → `setup` |
| Как ведущий ждёт ответы и что делает с неверными? | `moderator.cpp` → `ask`, `isValid`, `fallback` |
| Почему нити и корутины дают одинаковый вывод? | `player_runtime.cpp`; ответы собираются в `map` по номерам; свой `Rng` у каждого игрока |
| Как решается ночь? | `night.cpp` → `resolveNight`; цели — `bots.cpp`, `nightTargets` |
| Как бот выбирает, кого обвинить? | `bots.cpp` → `chooseStatement`, `chooseVote`, `observe` |
| Где проверка концептами? | `player_concepts.hpp`, `role_dispatch.hpp`, `src/player_concepts.cpp`, `tests/concept_violation.cpp` |
| Что видит модель и как проверяется её ответ? | `ai_agent.cpp` → `buildAgentPrompt`, `parseAgentReply` |
| Как устроены повтор и лимит 10 запросов? | `ai_agent.cpp` → `AgentBrain::decide`; `moderator.cpp` → `grantAiCalls`, `returnAiCalls` |
| Какой HTTP-запрос уходит в API? | `llm_protocol.cpp` → `buildChatRequest`; `llm_curl.cpp` → `complete` |
| Куда пишутся файловые логи? | `file_log.cpp`; включаются `--log-dir DIR` |
