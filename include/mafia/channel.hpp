#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>

namespace mafia {

// Потокобезопасная очередь сообщений: единственный способ общения
// ведущего с нитями игроков.
template <class T>
class Channel {
public:
    void push(T value) {
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(std::move(value));
        }
        ready_.notify_one();
    }

    // Блокируется, пока в очереди не появится сообщение.
    T pop() {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<T> queue_;
};

}  // namespace mafia
