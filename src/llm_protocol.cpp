// Протокол OpenAI-совместимого Chat Completions API без транспорта: тело
// запроса и разбор ответа. Так говорят OpenRouter, Groq, Gemini и Ollama.

#include <cstdlib>
#include <string>

#include "mafia/json.hpp"
#include "mafia/llm_client.hpp"

namespace mafia {

namespace {

std::string errorMessage(const json::Value& body) {
    if (const json::Value* error = body.find("error")) {
        if (error->isString()) {
            return error->asString();
        }
        if (const json::Value* message = error->find("message"); message && message->isString()) {
            return message->asString();
        }
        return "без описания";
    }
    return {};
}

// Рассуждающие модели (DeepSeek R1, Qwen3 и т. п.) иногда пишут рассуждение
// прямо в content: «<think>…</think>{…}». Ответ — то, что после блока.
std::string withoutThinking(std::string text) {
    constexpr std::string_view kClose = "</think>";
    if (const auto end = text.rfind(kClose); end != std::string::npos) {
        text.erase(0, end + kClose.size());
    }
    return text;
}

// content — строка или (у некоторых провайдеров) массив частей {type: text, text}.
std::string contentText(const json::Value& content) {
    if (content.isString()) {
        return content.asString();
    }
    std::string text;
    if (content.isArray()) {
        for (const json::Value& part : content.asArray()) {
            if (const json::Value* piece = part.find("text"); piece && piece->isString()) {
                text += piece->asString();
            }
        }
    }
    return text;
}

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

}  // namespace

std::string buildChatRequest(const LlmOptions& options, const LlmRequest& request) {
    using json::Array;
    using json::Object;
    Object body{{"model", options.model}};
    if (!options.fallbackModels.empty()) {
        Array models{options.model};
        models.insert(models.end(), options.fallbackModels.begin(), options.fallbackModels.end());
        body.emplace_back("models", std::move(models));
    }
    Array messages{Object{{"role", "system"}, {"content", request.system}},
                   Object{{"role", "user"}, {"content", request.prompt}}};
    if (!request.correction.empty()) {
        messages.emplace_back(Object{{"role", "assistant"}, {"content", request.previousReply}});
        messages.emplace_back(Object{{"role", "user"}, {"content", request.correction}});
    }
    body.insert(body.end(), {
        {"messages", std::move(messages)},
        // Режим JSON поддерживают все перечисленные провайдеры; схему ответа
        // описывает системный промпт, а ведущий всё равно проверяет ответ сам.
        {"response_format", Object{{"type", "json_object"}}},
        // Рассуждение входит в max_tokens: без запаса модель может истратить
        // всё на него и не дойти до ответа (finish_reason: length).
        {"max_tokens", 8000},
        {"temperature", 0.8},
    });
    if (!options.reasoningEffort.empty()) {
        body.emplace_back("reasoning", Object{{"effort", options.reasoningEffort}});
    }
    return json::Value(std::move(body)).dump();
}

LlmReply parseChatResponse(std::string_view body, long httpStatus) {
    LlmReply reply;
    json::Value parsed;
    try {
        parsed = json::parse(body);
    } catch (const json::ParseError& error) {
        reply.error = "ответ API не JSON (HTTP " + std::to_string(httpStatus) + "): " + error.what();
        return reply;
    }
    // Какая модель ответила: у OpenRouter это может быть запасная из поля models.
    if (const json::Value* model = parsed.find("model"); model && model->isString()) {
        reply.model = model->asString();
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        const std::string message = errorMessage(parsed);
        reply.error = "HTTP " + std::to_string(httpStatus) + (message.empty() ? "" : ": " + message);
        return reply;
    }
    // OpenRouter сообщает об ошибке поставщика модели и с кодом 200.
    if (const std::string message = errorMessage(parsed); !message.empty()) {
        reply.error = "ошибка API: " + message;
        return reply;
    }
    const json::Value* choices = parsed.find("choices");
    if (!choices || !choices->isArray() || choices->asArray().empty()) {
        reply.error = "в ответе API нет choices";
        return reply;
    }
    const json::Value& choice = choices->asArray().front();
    if (const json::Value* finish = choice.find("finish_reason"); finish && finish->isString()) {
        if (finish->asString() == "length") {
            reply.error = "ответ обрезан (finish_reason: length)";
            return reply;
        }
        if (finish->asString() == "content_filter") {
            reply.error = "ответ заблокирован фильтром (finish_reason: content_filter)";
            return reply;
        }
    }
    const json::Value* message = choice.find("message");
    if (!message) {
        reply.error = "в ответе API нет message";
        return reply;
    }
    if (const json::Value* refusal = message->find("refusal"); refusal && refusal->isString()) {
        reply.error = "модель отказалась отвечать: " + refusal->asString();
        return reply;
    }
    const json::Value* content = message->find("content");
    std::string text = content ? withoutThinking(contentText(*content)) : std::string();
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
        reply.error = "пустой ответ модели";
        return reply;
    }
    reply.ok = true;
    reply.text = std::move(text);
    return reply;
}

LlmOptions llmOptionsFromEnvironment(std::string model) {
    LlmOptions options;
    if (std::string fromEnvironment = environment("MAFIA_LLM_MODEL"); !fromEnvironment.empty()) {
        options.model = std::move(fromEnvironment);
    } else if (!model.empty()) {
        options.model = std::move(model);
    }
    if (std::string url = environment("MAFIA_LLM_BASE_URL"); !url.empty()) {
        while (url.ends_with('/')) {
            url.pop_back();
        }
        options.baseUrl = std::move(url);
    }
    // Запасные модели — только к модели по умолчанию и только у OpenRouter: поле
    // models — расширение OpenRouter, а выбранную вручную модель не подменяем.
    if (options.baseUrl == kDefaultLlmBaseUrl && options.model == kDefaultLlmModel) {
        options.fallbackModels.assign(kDefaultFallbackModels.begin(), kDefaultFallbackModels.end());
    }
    // Бесплатные модели OpenRouter — рассуждающие; на реплику хватает короткого
    // рассуждения, а длинное приводило к таймаутам и обрезанным ответам.
    if (options.baseUrl == kDefaultLlmBaseUrl) {
        options.reasoningEffort = "low";
    }
    options.apiKey = environment("MAFIA_LLM_API_KEY");
    // Ключ OpenRouter уходит только в OpenRouter: с другим адресом нужен MAFIA_LLM_API_KEY.
    if (options.apiKey.empty() && options.baseUrl == kDefaultLlmBaseUrl) {
        options.apiKey = environment("OPENROUTER_API_KEY");
    }
    options.traceFile = environment("MAFIA_LLM_TRACE");
    if (const std::string timeout = environment("MAFIA_LLM_TIMEOUT_MS"); !timeout.empty()) {
        const long value = std::strtol(timeout.c_str(), nullptr, 10);
        if (value > 0) {
            options.timeoutMs = value;
        }
    }
    return options;
}

}  // namespace mafia
