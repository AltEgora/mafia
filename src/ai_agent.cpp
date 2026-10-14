#include "mafia/ai_agent.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <ranges>

#include "mafia/game_events.hpp"
#include "mafia/ranges_util.hpp"

namespace mafia {

namespace {

constexpr std::size_t kMaxMessageChars = 300;
constexpr std::size_t kMaxReasoningChars = 500;

// Одна строка без управляющих символов, не длиннее limit символов (не байтов).
std::string sanitize(std::string_view text, std::size_t limit) {
    std::string out;
    std::size_t chars = 0;
    bool space = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 || c == 0x7F || c == ' ') {
            space = !out.empty();
            continue;
        }
        const bool leadByte = (c & 0xC0) != 0x80;
        if (leadByte) {
            if (chars == limit) {
                out += "…";
                return out;
            }
            if (space) {
                out += ' ';
                ++chars;
                space = false;
                if (chars == limit) {
                    out += "…";
                    return out;
                }
            }
            ++chars;
        }
        out += static_cast<char>(c);
    }
    return out;
}

json::Value nullable(int player) {
    return player > 0 ? json::Value(player) : json::Value(nullptr);
}

json::Value sideOf(const PublicEvent& event) {
    return event.side ? json::Value(sideName(*event.side)) : json::Value(nullptr);
}

json::Value roleOf(const PublicEvent& event) {
    return event.role ? json::Value(roleName(*event.role)) : json::Value(nullptr);
}

bool executed(const PublicEvent& death) {
    return contains(death.causes, DeathCause::Execution);
}

// Событие истории в том виде, в каком его видели все за столом.
json::Value describe(const PublicEvent& event) {
    using json::Object;
    switch (event.type) {
        case EventType::Statement:
            return Object{{"type", "statement"},
                          {"player", event.actor},
                          {"text", event.text},
                          {"accuses", event.statement == ActionType::Accuse ? nullable(event.target) : nullptr}};
        case EventType::Vote:
            return Object{{"type", "vote"}, {"player", event.actor}, {"against", event.target}};
        case EventType::Death: {
            json::Array causes = toVector(event.causes | std::views::filter([](DeathCause cause) {
                                              return cause != DeathCause::Unknown && cause != DeathCause::Execution;
                                          }) |
                                          std::views::transform([](DeathCause cause) { return json::Value(causeName(cause)); }));
            return Object{{"type", "elimination"},
                          {"player", event.target},
                          {"how", executed(event) ? "изгнан голосованием" : "погиб ночью"},
                          {"side", sideOf(event)},
                          {"role", roleOf(event)},
                          {"killed_by", std::move(causes)}};
        }
        case EventType::Saved:
            return Object{{"type", "saved_at_night"}, {"player", event.target}};
        case EventType::Abstain:
            return Object{{"type", "abstain"}, {"player", event.actor}, {"text", event.text}};
    }
    return nullptr;
}

std::string idsText(const std::vector<int>& ids) {
    std::string text;
    for (int id : ids) {
        text += (text.empty() ? "" : ", ") + std::to_string(id);
    }
    return text;
}

// Первое слово латиницей (две латинские буквы подряд и больше) или пусто.
std::string_view latinWord(std::string_view text) {
    const auto latin = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    for (std::size_t i = 0; i < text.size();) {
        if (!latin(text[i])) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < text.size() && latin(text[end])) {
            ++end;
        }
        if (end - i >= 2) {
            return text.substr(i, end - i);
        }
        i = end;
    }
    return {};
}

bool cyrillicByte(char c) {
    const auto byte = static_cast<unsigned char>(c);
    return byte == 0xD0 || byte == 0xD1;
}

}  // namespace

// Номера игроков, названных в реплике: «игрок 3», «Игрока 5», «игроком №2»,
// «игроки 2, 4 и 6». Числа без слова «игрок» номерами не считаются.
std::vector<int> namedPlayers(std::string_view text) {
    std::vector<int> ids;
    const auto skip = [&text](std::size_t at, std::string_view what) {
        return text.substr(at).starts_with(what) ? at + what.size() : at;
    };
    for (std::size_t at = 0; at < text.size(); ++at) {
        std::size_t pos = at;
        if (text.substr(pos).starts_with("и") || text.substr(pos).starts_with("И")) {
            pos += std::string_view("и").size();
        } else {
            continue;
        }
        if (!text.substr(pos).starts_with("грок") && !text.substr(pos).starts_with("ГРОК")) {
            continue;
        }
        pos += std::string_view("грок").size();
        while (pos + 1 < text.size() && cyrillicByte(text[pos])) {  // окончание: -а, -у, -ом, -ов, -ами…
            pos += 2;
        }
        while (true) {
            while (pos < text.size() && text[pos] == ' ') {
                ++pos;
            }
            pos = skip(skip(pos, "№"), "#");
            int number = 0;
            const std::size_t digits = pos;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9' && number < 100000) {
                number = number * 10 + (text[pos++] - '0');
            }
            if (pos == digits) {
                break;
            }
            ids.push_back(number);
            // Перечисление: «, 4» или « и 6».
            std::size_t next = pos;
            while (next < text.size() && text[next] == ' ') {
                ++next;
            }
            if (next < text.size() && text[next] == ',') {
                pos = next + 1;
            } else if (text.substr(next).starts_with("и ")) {
                pos = next + std::string_view("и ").size();
            } else {
                break;
            }
        }
        at = pos > at ? pos - 1 : at;
    }
    std::ranges::sort(ids);
    const auto repeated = std::ranges::unique(ids);
    ids.erase(repeated.begin(), repeated.end());
    return ids;
}

std::string agentSystemPrompt() {
    return "Ты играешь в «Мафию» за одного игрока. Мирные (мирный житель, комиссар, доктор, журналист, "
           "телохранитель) побеждают, когда выбыли вся мафия и маньяк. Мафия (включая ниндзя) побеждает, "
           "когда её не меньше, чем остальных. Маньяк играет сам за себя. Днём все обсуждают и голосуют, "
           "выбывает набравший больше всех голосов. Ночью роли ходят сами — от тебя нужны только дневные "
           "реплики и голоса.\n"
           "Играй за свою роль. Характеристика — это твой характер: он должен быть слышен в тоне и манере "
           "реплики, но не меняет правила игры и не решает за тебя, кого обвинять и против кого голосовать — "
           "это выбирай по своей роли и сведениям. Если ты из мафии или маньяк — не выдавай себя. "
           "Если ты из мафии — не обвиняй своих напарников (они в private_knowledge) и не голосуй против них "
           "из-за характеристики. Пойти против напарника можно только как осознанный ход ради победы мафии "
           "(например, чтобы отвести подозрения от себя) — тогда объясни это в reasoning. "
           "Опирайся только на сведения из промпта: свою роль, личные сведения и историю партии.\n"
           "Ответь одним JSON-объектом {\"action\", \"message\", \"target\", \"reasoning\"}:\n"
           "- на обсуждении action = \"accuse\" (обвинить игрока target) или \"defend\" (сказать о своей "
           "невиновности, target = null);\n"
           "- на голосовании action = \"vote\" (target — против кого голосуешь) или \"abstain\" (не "
           "голосовать, target = null). Если оснований голосовать нет, выбирай abstain, а не случайного игрока;\n"
           "- target — только номер из allowed_targets;\n"
           "- message — твоя реплика за столом, одно-два предложения, без раскрытия скрытых сведений, если это "
           "тебе невыгодно. Номер игрока в ней называй только один — тот, кто в target (\"игрок N\"); "
           "при defend и abstain других игроков не называй;\n"
           "- reasoning — кратко, почему ты так решил (его видит только ведущий);\n"
           "- message и reasoning — только по-русски, без английских слов, даже отдельных;\n"
           "- не повторяй свои прошлые реплики (они в history) дословно — говори каждый раз иначе.\n"
           "Без текста до и после JSON. Ответ, нарушающий эти правила, не засчитывается.";
}

json::Value buildAgentPrompt(const AgentIdentity& identity, const std::vector<std::string>& privateNotes,
                             const Request& request) {
    using json::Array;
    using json::Object;
    const GameView& view = *request.view;
    const bool discussion = request.type == RequestType::Discussion;

    auto alivePlayers = view.players | std::views::filter(&PlayerPublicInfo::alive);
    Array alive = toVector(alivePlayers | std::views::transform([](const PlayerPublicInfo& p) { return json::Value(p.id); }));
    Array eliminated = toVector(
        view.players | std::views::filter([](const PlayerPublicInfo& p) { return !p.alive; }) |
        std::views::transform([](const PlayerPublicInfo& p) {
            return json::Value(Object{
                {"player", p.id},
                {"side", p.revealedSide ? json::Value(sideName(*p.revealedSide)) : json::Value(nullptr)},
                {"role", p.revealedRole ? json::Value(roleName(*p.revealedRole)) : json::Value(nullptr)}});
        }));

    // История по раундам (дням) — только публичные события.
    std::map<int, Array> rounds;
    for (const PublicEvent& event : view.events) {
        rounds[event.day].push_back(describe(event));
    }
    Array history = toVector(rounds | std::views::transform([](const auto& round) {
                                 return json::Value(Object{{"day", round.first}, {"events", round.second}});
                             }));

    return Object{
        {"you",
         Object{{"player", identity.self},
                {"role", roleName(identity.role)},
                {"team", teamName(teamOf(identity.role))},
                {"characteristic", identity.characteristic}}},
        {"phase",
         Object{{"day", view.day},
                {"stage", discussion ? "discussion" : "vote"},
                {"task", discussion ? "Скажи одну реплику: обвини кого-то (accuse) или защити себя (defend)."
                                    : "Проголосуй, кого выгнать из города (vote), или воздержись (abstain), "
                                      "если оснований нет."}}},
        {"open_announcements", view.openAnnouncements},
        {"alive_players", std::move(alive)},
        {"eliminated", std::move(eliminated)},
        {"private_knowledge", toVector(privateNotes | std::views::transform([](const std::string& note) {
                                           return json::Value(note);
                                       }))},
        {"history", std::move(history)},
        {"allowed_targets", toVector(request.targets | std::views::transform([](int id) { return json::Value(id); }))},
        {"allowed_actions", discussion ? json::Array{"accuse", "defend"} : json::Array{"vote", "abstain"}},
    };
}

AgentDecision parseAgentReply(std::string_view text, const Request& request, int self) {
    const auto fail = [](std::string why) { return AgentDecision{std::nullopt, std::move(why)}; };

    // Модель может обернуть JSON в ```json …``` — берём от первой { до последней }.
    // Нет закрывающей } — ответ обрезан: разбираем до конца, парсер укажет ошибку.
    const std::size_t open = text.find('{');
    if (open == std::string_view::npos) {
        return fail("в ответе нет JSON-объекта");
    }
    const std::size_t close = text.rfind('}');
    const std::string_view candidate =
        close == std::string_view::npos || close < open ? text.substr(open) : text.substr(open, close - open + 1);
    json::Value reply;
    try {
        reply = json::parse(candidate);
    } catch (const json::ParseError& error) {
        return fail(std::string("битый JSON (") + error.what() + ")");
    }
    if (!reply.isObject()) {
        return fail("ответ — не JSON-объект");
    }

    const json::Value* action = reply.find("action");
    if (action == nullptr || !action->isString()) {
        return fail("нет поля action");
    }
    const auto optionalString = [&reply](std::string_view key) -> std::optional<std::string> {
        const json::Value* value = reply.find(key);
        if (value == nullptr || value->isNull()) {
            return std::string();
        }
        return value->isString() ? std::optional(value->asString()) : std::nullopt;
    };
    const std::optional<std::string> message = optionalString("message");
    const std::optional<std::string> reasoning = optionalString("reasoning");
    if (!message || !reasoning) {
        return fail("message и reasoning должны быть строками");
    }

    int target = 0;
    if (const json::Value* value = reply.find("target"); value != nullptr && !value->isNull()) {
        if (!value->isNumber() || value->asNumber() != std::floor(value->asNumber()) || value->asNumber() < 1 ||
            value->asNumber() > 100000) {
            return fail("target должен быть номером игрока");
        }
        target = static_cast<int>(value->asNumber());
    }

    Response response;
    const std::string& name = action->asString();
    if (request.type == RequestType::Discussion && name == "defend") {
        response.action = ActionType::ClaimInnocent;
        target = 0;
    } else if (request.type == RequestType::Discussion && name == "accuse") {
        response.action = ActionType::Accuse;
    } else if (request.type == RequestType::DayVote && name == "vote") {
        response.action = ActionType::Vote;
    } else if (request.type == RequestType::DayVote && name == "abstain") {
        if (target != 0) {
            return fail("при abstain target должен быть null");
        }
        response.action = ActionType::Abstain;
    } else {
        return fail("действие «" + name + "» не подходит этапу");
    }

    if (response.action == ActionType::Accuse || response.action == ActionType::Vote) {
        if (target == 0) {
            return fail("нет цели");
        }
        if (!request.view->isAlive(target)) {
            return fail("цель — не живой игрок (" + std::to_string(target) + ")");
        }
        if (!contains(request.targets, target)) {
            return fail("цель " + std::to_string(target) + " недоступна");
        }
    }
    response.target = target;
    response.message = sanitize(*message, kMaxMessageChars);
    const std::string why = sanitize(*reasoning, kMaxReasoningChars);

    // Только по-русски: английское слово в реплике или рассуждении — ответ не годится.
    for (const std::string_view field : {std::string_view(response.message), std::string_view(why)}) {
        if (const std::string_view word = latinWord(field); !word.empty()) {
            return fail("английское слово «" + std::string(word) + "» — отвечай только по-русски");
        }
    }
    // Реплика совпадает с ходом: назван только тот, кто в target; при defend и
    // abstain — никто, кроме самого агента.
    const std::vector<int> named = namedPlayers(response.message);
    if (response.action == ActionType::Accuse || response.action == ActionType::Vote) {
        if (const std::vector<int> others = allExcept(named, target); !others.empty()) {
            return fail("в message назван игрок " + idsText(others) + ", а target — " + std::to_string(target) +
                        ": называй только того, кто в target");
        }
    } else if (const std::vector<int> others = allExcept(named, self); !others.empty()) {
        return fail(std::string("при ") + (response.action == ActionType::Abstain ? "abstain" : "defend") +
                    " в message назван игрок " + idsText(others) + ": других игроков не называй");
    }

    response.reason = "ИИ: " + (why.empty() ? std::string("без пояснений") : why);
    return AgentDecision{std::move(response), {}};
}

// ---------------------------------------------------------------- AgentBrain

AgentBrain::AgentBrain(AgentIdentity identity, SharedPtr<LlmClient> client)
    : identity_(std::move(identity)), client_(std::move(client)) {}

std::optional<Response> AgentBrain::decide(const Request& request, std::string& fallbackReason) {
    fallbackReason.clear();
    lastCalls_ = 0;
    if (!client_ || !client_->available()) {
        return std::nullopt;  // API недоступно вовсе — молча играем ботом
    }
    if ((request.type != RequestType::Discussion && request.type != RequestType::DayVote) || !request.view) {
        return std::nullopt;
    }
    if (request.aiCalls <= 0) {
        fallbackReason = "квота запросов раунда исчерпана";
        return std::nullopt;
    }
    const int round = request.view->day;
    if (round != round_) {
        round_ = round;
        callsThisRound_ = 0;
    }

    LlmRequest llmRequest{.system = agentSystemPrompt(),
                          .prompt = buildAgentPrompt(identity_, notes_, request).dump(),
                          .round = round,
                          .player = identity_.self};
    // Какая модель ответила — в лог; запасная (OpenRouter, поле models) — отдельно.
    const std::string requested = client_->model();
    const auto modelNote = [&requested](const LlmReply& reply) -> std::string {
        if (reply.model.empty()) {
            return {};
        }
        return answeredByFallback(reply, requested)
                   ? " (ответила запасная модель " + reply.model + " вместо " + requested + ")"
                   : " (модель " + reply.model + ")";
    };
    // Причина, по которой отклонён первый ответ, — она же пояснение для повтора.
    std::string rejected;
    std::string rejectedNote;  // какая модель дала отклонённый ответ
    const auto withRejected = [&rejected, &rejectedNote](std::string why) {
        return rejected.empty() ? why : "ответ отклонён: " + rejected + rejectedNote + "; повтор: " + why;
    };
    for (int attempt = 1;; ++attempt) {
        if (callsThisRound_ >= kMaxCallsPerRound) {
            fallbackReason = withRejected("лимит " + std::to_string(kMaxCallsPerRound) + " запросов за раунд");
            return std::nullopt;
        }
        ++callsThisRound_;
        ++totalCalls_;
        ++lastCalls_;

        LlmReply reply;
        try {
            reply = client_->complete(llmRequest);
        } catch (const std::exception& error) {
            fallbackReason = withRejected(std::string("ошибка клиента: ") + error.what());
            return std::nullopt;
        }
        // Сеть, таймаут, HTTP-ошибка — не повторяем: это не ошибка в ответе модели.
        if (!reply.ok) {
            fallbackReason = withRejected(reply.error + modelNote(reply));
            return std::nullopt;
        }
        AgentDecision decision = parseAgentReply(reply.text, request, identity_.self);
        if (decision.response) {
            const std::string why = decision.response->reason.substr(std::string_view("ИИ: ").size());
            decision.response->reason =
                std::string(attempt > 1 ? "ИИ со второй попытки" : "ИИ") + modelNote(reply) + ": " + why;
            return decision.response;
        }
        if (!rejected.empty()) {
            fallbackReason = "ответ отклонён дважды: " + rejected + rejectedNote + "; " + decision.failure +
                             modelNote(reply);
            return std::nullopt;
        }
        rejected = decision.failure;
        rejectedNote = modelNote(reply);
        if (attempt >= kMaxAttempts || attempt >= request.aiCalls) {
            fallbackReason =
                "ответ отклонён: " + rejected + rejectedNote + "; повтора нет — квота запросов раунда исчерпана";
            return std::nullopt;
        }
        // Один повтор: модель видит свой ответ и что в нём не так.
        llmRequest.previousReply = reply.text;
        llmRequest.correction = "Твой ответ не принят: " + rejected +
                                ". Пришли исправленный ответ — один JSON-объект по тем же правилам.";
    }
}

void AgentBrain::rememberInfo(const Request& info) {
    if (info.check) {
        notes_.push_back("Твоя проверка: игрок " + std::to_string(info.check->target) + " — " +
                         (info.check->isMafia ? "мафия" : "не мафия"));
    }
    if (info.comparison) {
        notes_.push_back("Твоё сравнение: игроки " + std::to_string(info.comparison->first) + " и " +
                         std::to_string(info.comparison->second) + " — " +
                         (info.comparison->same ? "одинаковый статус" : "разный статус"));
    }
}

void AgentBrain::rememberNightMove(int night, const Response& move) {
    const std::string target = "игрока " + std::to_string(move.target);
    std::string text;
    switch (move.action) {
        case ActionType::Kill:
            text = (teamOf(identity_.role) == Team::Mafia ? "предложил убить " : "напал на ") + target;
            break;
        case ActionType::Heal:
            text = "лечил " + target;
            break;
        case ActionType::Check:
            text = "проверял " + target;
            break;
        case ActionType::Shoot:
            text = "стрелял в " + target;
            break;
        case ActionType::Guard:
            text = "прикрывал " + target;
            break;
        case ActionType::Compare:
            text = "сравнивал игроков " + std::to_string(move.target) + " и " + std::to_string(move.secondTarget);
            break;
        default:
            return;
    }
    notes_.push_back("Ночь " + std::to_string(night) + ": ты " + text);
}

void AgentBrain::rememberTeam(const std::vector<int>& team) {
    const std::vector<int> partners = allExcept(team, identity_.self);
    notes_.push_back(partners.empty() ? std::string("Ты единственный мафиози")
                                      : "Твои напарники по мафии: игроки " + idsText(partners));
}

}  // namespace mafia
