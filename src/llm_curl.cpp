// Транспорт для Chat Completions API. С libcurl (MAFIA_HAVE_CURL) — настоящий
// HTTPS-клиент; без неё — клиент, который сообщает, что API недоступен.

#include <fstream>
#include <mutex>
#include <string>
#include <utility>

#include "mafia/json.hpp"
#include "mafia/llm_client.hpp"

#ifndef MAFIA_HAVE_CURL
#define MAFIA_HAVE_CURL 0
#endif

#if MAFIA_HAVE_CURL
#include <curl/curl.h>
#endif

namespace mafia {

namespace {

// API недоступен: агенты сразу играют как обычные боты.
class UnavailableClient final : public LlmClient {
public:
    UnavailableClient(std::string model, std::string reason) : model_(std::move(model)), reason_(std::move(reason)) {}

    bool available() const override { return false; }
    std::string unavailableReason() const override { return reason_; }
    std::string model() const override { return model_; }
    LlmReply complete(const LlmRequest&) override { return LlmReply{.ok = false, .text = {}, .error = reason_}; }

private:
    std::string model_;
    std::string reason_;
};

#if MAFIA_HAVE_CURL

std::size_t collect(char* data, std::size_t size, std::size_t count, void* userdata) {
    static_cast<std::string*>(userdata)->append(data, size * count);
    return size * count;
}

// Каждый запрос — свой CURL easy handle, поэтому клиент можно вызывать из
// нескольких нитей одновременно. curl_global_init — один раз, до первой нити.
class CurlChatClient final : public LlmClient {
public:
    explicit CurlChatClient(LlmOptions options) : options_(std::move(options)) {
        static std::once_flag initialized;
        std::call_once(initialized, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    }

    bool available() const override { return true; }
    std::string model() const override { return options_.model; }

    LlmReply complete(const LlmRequest& request) override {
        CURL* curl = curl_easy_init();
        if (curl == nullptr) {
            return LlmReply{.ok = false, .text = {}, .error = "не удалось создать запрос libcurl"};
        }
        const std::string body = buildChatRequest(options_, request);
        const std::string url = options_.baseUrl + "/chat/completions";
        const std::string keyHeader = "authorization: Bearer " + options_.apiKey;
        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "content-type: application/json");
        if (!options_.apiKey.empty()) {  // локальной Ollama ключ не нужен
            headers = curl_slist_append(headers, keyHeader.c_str());
        }

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, options_.timeoutMs);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // таймауты без сигналов — безопасно в нитях

        const CURLcode code = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (code != CURLE_OK) {
            const std::string error = std::string("сеть: ") + curl_easy_strerror(code);
            trace(request, body, status, error, LlmReply{});
            return LlmReply{.ok = false, .text = {}, .error = error};
        }
        LlmReply reply = parseChatResponse(response, status);
        trace(request, body, status, response, reply);
        return reply;
    }

private:
    // Отладка: одна JSON-строка на запрос — тело запроса, сырой ответ и какая
    // модель ответила (запрошенная или запасная). Ключ в тело не входит (он
    // только в заголовке), поэтому в файл не попадает.
    void trace(const LlmRequest& request, const std::string& body, long status, const std::string& response,
               const LlmReply& reply) {
        if (options_.traceFile.empty()) {
            return;
        }
        const json::Value line = json::Object{
            {"day", request.round},
            {"player", request.player},
            {"status", static_cast<double>(status)},
            {"model_requested", options_.model},
            {"model_answered", reply.model.empty() ? json::Value(nullptr) : json::Value(reply.model)},
            {"fallback_model", answeredByFallback(reply, options_.model)},
            {"request", body},
            {"response", response},
        };
        const std::lock_guard lock(traceMutex_);
        std::ofstream(options_.traceFile, std::ios::app) << line.dump() << '\n';
    }

    LlmOptions options_;
    std::mutex traceMutex_;
};

#endif

}  // namespace

bool builtWithCurl() noexcept {
    return MAFIA_HAVE_CURL != 0;
}

SharedPtr<LlmClient> makeLlmClient(const LlmOptions& options) {
#if MAFIA_HAVE_CURL
    if (options.apiKey.empty() && options.baseUrl == kDefaultLlmBaseUrl) {
        return MakeShared<UnavailableClient>(options.model, "не задан ключ OPENROUTER_API_KEY");
    }
    return MakeShared<CurlChatClient>(options);
#else
    return MakeShared<UnavailableClient>(options.model, "проект собран без libcurl");
#endif
}

}  // namespace mafia
