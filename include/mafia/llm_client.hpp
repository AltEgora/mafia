#pragma once

// Клиент языковой модели для ИИ-агентов. Интерфейс LlmClient позволяет
// подставлять в тестах мок; реальный клиент ходит по HTTPS через libcurl в
// OpenAI-совместимый Chat Completions API. По умолчанию это бесплатная модель
// OpenRouter с запасными бесплатными моделями; через MAFIA_LLM_BASE_URL подходят и
// другие бесплатные API — Groq, Gemini, локальная Ollama. Если проект собран
// без libcurl или нет ключа, клиент сообщает, что недоступен, и агенты играют
// как обычные боты.

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "mafia/shared_ptr.hpp"

namespace mafia {

struct LlmRequest {
    std::string system;  // правила и формат ответа
    std::string prompt;  // структурированный промпт (JSON)
    int round = 0;       // день партии — для журналов и тестов
    int player = 0;      // номер агента
    // Повтор после отклонённого ответа: что модель ответила и что в этом не так.
    // Уходят в диалог как реплика ассистента и новое сообщение пользователя.
    std::string previousReply;
    std::string correction;
};

struct LlmReply {
    bool ok = false;
    std::string text;   // текст ответа модели (ожидается JSON)
    std::string error;  // почему ответа нет: сеть, таймаут, HTTP-ошибка, отказ модели
    std::string model;  // какая модель ответила (поле model ответа API); пусто — неизвестно
};

// Ответила не запрошенная модель, а запасная (OpenRouter, поле models).
inline bool answeredByFallback(const LlmReply& reply, std::string_view requested) {
    return !reply.model.empty() && reply.model != requested;
}

class LlmClient {
public:
    virtual ~LlmClient() = default;

    // Можно ли вообще обращаться к API (есть ключ, проект собран с libcurl).
    virtual bool available() const = 0;
    virtual std::string unavailableReason() const { return {}; }
    virtual std::string model() const = 0;

    // Синхронный запрос. Должен быть потокобезопасным: в режиме нитей агенты
    // обращаются к одному клиенту из разных нитей.
    virtual LlmReply complete(const LlmRequest& request) = 0;
};

// Бесплатная модель OpenRouter по умолчанию. Бесплатные модели бывают перегружены
// (HTTP 429) и пропадают из каталога, поэтому с ней OpenRouter получает список
// запасных (поле models): следующая берётся, если предыдущая вернула ошибку.
// Роутер openrouter/free сюда не входит: он выбирает модель случайно и попадал
// на классификатор безопасности и крошечные модели, которые не отвечают JSON.
inline constexpr std::string_view kDefaultLlmModel = "nvidia/nemotron-3-super-120b-a12b:free";
inline constexpr std::array<std::string_view, 2> kDefaultFallbackModels = {"nvidia/nemotron-3.5-lightning:free",
                                                                           "google/gemma-4-31b-it:free"};
inline constexpr std::string_view kDefaultLlmBaseUrl = "https://openrouter.ai/api/v1";

struct LlmOptions {
    std::string model{kDefaultLlmModel};      // MAFIA_LLM_MODEL или ai_agents.model
    std::string apiKey;                       // MAFIA_LLM_API_KEY или OPENROUTER_API_KEY
    std::string baseUrl{kDefaultLlmBaseUrl};  // MAFIA_LLM_BASE_URL — другой OpenAI-совместимый API
    long timeoutMs = 90000;                   // MAFIA_LLM_TIMEOUT_MS
    std::string traceFile;                    // MAFIA_LLM_TRACE — дописывать запросы и ответы (без ключа)
    std::vector<std::string> fallbackModels;  // только OpenRouter: запасные модели (поле models)
    std::string reasoningEffort;              // только OpenRouter: длина рассуждения (поле reasoning)
};

// Ключ и настройки из переменных окружения. Модель: MAFIA_LLM_MODEL, иначе
// аргумент (поле model из YAML), иначе по умолчанию — тогда с запасными моделями
// (если адрес — OpenRouter).
LlmOptions llmOptionsFromEnvironment(std::string model);

// Реальный клиент (libcurl) или «недоступный». Ключ обязателен только для
// OpenRouter по умолчанию: локальной Ollama (MAFIA_LLM_BASE_URL) он не нужен.
SharedPtr<LlmClient> makeLlmClient(const LlmOptions& options);

bool builtWithCurl() noexcept;

// --- Протокол Chat Completions: чистые функции, тестируются без сети.

// Тело POST {baseUrl}/chat/completions: модель (и запасные, если заданы),
// системный промпт, промпт игрока, ответ — JSON-объект (response_format: json_object),
// длина рассуждения (если задана).
std::string buildChatRequest(const LlmOptions& options, const LlmRequest& request);

// Разбор ответа: текст choices[0].message.content без блока <think>…</think>
// и имя ответившей модели (поле model) — даже если ответ не годится;
// обрезанный ответ, фильтр контента и HTTP-ошибки превращаются в ok = false
// с понятной причиной.
LlmReply parseChatResponse(std::string_view body, long httpStatus);

}  // namespace mafia
