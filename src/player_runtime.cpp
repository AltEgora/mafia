#include "mafia/player_runtime.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace mafia {

// ------------------------------------------------------------ ThreadRuntime

ThreadRuntime::ThreadRuntime() : responses_(MakeShared<ResponseChannel>()) {}

ThreadRuntime::~ThreadRuntime() {
    stop();
}

void ThreadRuntime::start(const std::vector<SharedPtr<Player>>& players) {
    players_ = players;
    for (const auto& player : players_) {
        inboxes_.push_back(MakeShared<RequestChannel>());
        player->start(inboxes_.back(), responses_);
    }
    running_ = true;
}

void ThreadRuntime::send(int player, Request request) {
    inboxes_[static_cast<std::size_t>(player - 1)]->push(std::move(request));
}

Response ThreadRuntime::receive() {
    return responses_->pop();
}

void ThreadRuntime::stop() noexcept {
    if (!running_) {
        return;
    }
    for (auto& inbox : inboxes_) {
        inbox->push(Request{.type = RequestType::Stop});
    }
    for (auto& player : players_) {
        player->join();
    }
    running_ = false;
}

// --------------------------------------------------------- CoroutineRuntime

CoroutineRuntime::CoroutineRuntime(coro::AsyncInput::Reader readLine)
    : scheduler_(MakeShared<coro::Scheduler>()),
      input_(MakeShared<coro::AsyncInput>(scheduler_, std::move(readLine))),
      responses_(MakeShared<ResponseCoChannel>(scheduler_)) {}

CoroutineRuntime::~CoroutineRuntime() {
    stop();
}

coro::AsyncInput::Reader CoroutineRuntime::readerFor(std::istream& in) {
    return [&in]() -> std::optional<std::string> {
        std::string line;
        if (!std::getline(in, line)) {
            return std::nullopt;
        }
        return line;
    };
}

void CoroutineRuntime::start(const std::vector<SharedPtr<Player>>& players) {
    for (const auto& player : players) {
        inboxes_.push_back(MakeShared<RequestCoChannel>(scheduler_));
        tasks_.push_back(player->play(inboxes_.back(), responses_));
        scheduler_->schedule(tasks_.back().handle());  // дойдёт до ожидания первого запроса
    }
    running_ = true;
    while (scheduler_->step()) {
        ++steps_;
    }
    checkTasks();
}

void CoroutineRuntime::send(int player, Request request) {
    inboxes_[static_cast<std::size_t>(player - 1)]->push(std::move(request));
}

Response CoroutineRuntime::receive() {
    for (;;) {
        if (auto response = responses_->tryPop()) {
            return std::move(*response);
        }
        if (scheduler_->step()) {  // пошагово продвигаем готовые корутины
            ++steps_;
            checkTasks();
            continue;
        }
        if (input_->waiting()) {  // остался только человек — читаем его строку
            input_->deliver();
            continue;
        }
        throw std::logic_error("все корутины игроков ждут, но ответа нет");
    }
}

void CoroutineRuntime::stop() noexcept {
    if (!running_) {
        return;
    }
    for (auto& inbox : inboxes_) {
        inbox->push(Request{.type = RequestType::Stop});
    }
    while (scheduler_->step()) {
        ++steps_;
    }
    tasks_.clear();  // кадры завершённых корутин уничтожаются вместе с задачами
    running_ = false;
}

// Исключение, вылетевшее из корутины игрока, передаём ведущему.
void CoroutineRuntime::checkTasks() const {
    for (const auto& task : tasks_) {
        task.rethrowIfFailed();
    }
}

}  // namespace mafia
