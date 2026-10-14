#pragma once

// Корутины C++20 без сторонних библиотек: задача Task<T>, однопоточный
// планировщик, канал сообщений и асинхронный ввод человека. Всё работает
// в одной (главной) нити: корутины уступают управление на co_await, а
// ведущий пошагово возобновляет их через Scheduler.

#include <coroutine>
#include <deque>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "mafia/shared_ptr.hpp"

namespace mafia::coro {

namespace detail {

struct PromiseBase {
    std::coroutine_handle<> continuation;  // кто ждёт эту задачу через co_await
    std::exception_ptr exception;

    // Задача ленивая: тело начинает выполняться только при co_await или resume().
    std::suspend_always initial_suspend() const noexcept { return {}; }

    // По завершении управление сразу передаётся ожидающей корутине
    // (симметричная передача); корневая задача возвращается в планировщик.
    struct FinalAwaiter {
        bool await_ready() const noexcept { return false; }
        template <class Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> self) const noexcept {
            const std::coroutine_handle<> next = self.promise().continuation;
            return next ? next : std::noop_coroutine();
        }
        void await_resume() const noexcept {}
    };
    FinalAwaiter final_suspend() const noexcept { return {}; }

    void unhandled_exception() noexcept { exception = std::current_exception(); }

    void rethrowIfFailed() const {
        if (exception) {
            std::rethrow_exception(exception);
        }
    }
};

template <class T>
struct Promise : PromiseBase {
    std::optional<T> value;

    template <class U>
    void return_value(U&& result) {
        value.emplace(std::forward<U>(result));
    }

    T result() {
        rethrowIfFailed();
        return std::move(*value);
    }
};

template <>
struct Promise<void> : PromiseBase {
    void return_void() const noexcept {}
    void result() const { rethrowIfFailed(); }
};

}  // namespace detail

// Задача-корутина. Владеет кадром корутины (уничтожает его в деструкторе),
// только перемещается. Её можно ждать из другой корутины (co_await task) или
// запускать как корневую через планировщик (handle()).
template <class T = void>
class [[nodiscard]] Task {
public:
    struct promise_type : detail::Promise<T> {
        Task get_return_object() noexcept {
            return Task(std::coroutine_handle<promise_type>::from_promise(*this));
        }
    };

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { destroy(); }

    // co_await task: запускает задачу, а по её завершении продолжает ожидающего.
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        handle_.promise().continuation = awaiting;
        return handle_;
    }
    T await_resume() { return handle_.promise().result(); }

    // Для корневой задачи, которую возобновляет планировщик.
    std::coroutine_handle<> handle() const noexcept { return handle_; }
    bool done() const noexcept { return !handle_ || handle_.done(); }
    void rethrowIfFailed() const {
        if (handle_ && handle_.done()) {
            handle_.promise().rethrowIfFailed();
        }
    }

private:
    explicit Task(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {}

    void destroy() noexcept {
        if (handle_) {
            handle_.destroy();
            handle_ = {};
        }
    }

    std::coroutine_handle<promise_type> handle_;
};

// Очередь готовых к продолжению корутин. Однопоточный: и планирование, и
// возобновление происходят в нити ведущего.
class Scheduler {
public:
    void schedule(std::coroutine_handle<> handle) { ready_.push_back(handle); }
    bool hasReady() const noexcept { return !ready_.empty(); }

    // Один шаг: возобновить следующую готовую корутину. false — готовых нет.
    bool step() {
        if (ready_.empty()) {
            return false;
        }
        const std::coroutine_handle<> next = ready_.front();
        ready_.pop_front();
        next.resume();
        return true;
    }

    void runUntilIdle() {
        while (step()) {
        }
    }

private:
    std::deque<std::coroutine_handle<>> ready_;
};

// Канал сообщений для корутин: получатель засыпает на co_await receive(),
// пока в очереди пусто; push() будит его через планировщик.
template <class T>
class CoChannel {
public:
    explicit CoChannel(SharedPtr<Scheduler> scheduler) : scheduler_(std::move(scheduler)) {}

    void push(T value) {
        queue_.push_back(std::move(value));
        if (waiter_) {
            scheduler_->schedule(std::exchange(waiter_, {}));
        }
    }

    // Без ожидания — для ведущего, который сам не корутина.
    std::optional<T> tryPop() {
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    auto receive() {
        struct Awaiter {
            CoChannel& channel;
            bool await_ready() const noexcept { return !channel.queue_.empty(); }
            void await_suspend(std::coroutine_handle<> awaiting) noexcept { channel.waiter_ = awaiting; }
            T await_resume() { return *channel.tryPop(); }
        };
        return Awaiter{*this};
    }

private:
    SharedPtr<Scheduler> scheduler_;
    std::deque<T> queue_;
    std::coroutine_handle<> waiter_;  // единственный получатель
};

// Ввод игрока-человека для режима корутин. Корутина человека засыпает на
// co_await nextLine(); строку читает ведущий (deliver), когда продвигать больше
// некого, — поэтому ожидание ввода не задерживает ходы ботов.
class AsyncInput {
public:
    using Reader = std::function<std::optional<std::string>()>;  // nullopt — ввод закончился

    AsyncInput(SharedPtr<Scheduler> scheduler, Reader reader)
        : scheduler_(std::move(scheduler)), reader_(std::move(reader)) {}

    bool waiting() const noexcept { return static_cast<bool>(waiter_); }

    void deliver() {
        line_ = reader_();
        scheduler_->schedule(std::exchange(waiter_, {}));
    }

    auto nextLine() {
        struct Awaiter {
            AsyncInput& input;
            bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<> awaiting) noexcept { input.waiter_ = awaiting; }
            std::optional<std::string> await_resume() { return std::exchange(input.line_, std::nullopt); }
        };
        return Awaiter{*this};
    }

private:
    SharedPtr<Scheduler> scheduler_;
    Reader reader_;
    std::coroutine_handle<> waiter_;
    std::optional<std::string> line_;
};

}  // namespace mafia::coro
