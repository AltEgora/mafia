#pragma once

// Как исполняются игроки: в своих нитях (по умолчанию) или как корутины
// (--coroutines). Ведущий работает с обоими режимами через один интерфейс:
// отправить запрос игроку, получить следующий ответ, остановить всех.

#include <cstdint>
#include <istream>
#include <vector>

#include "mafia/coro.hpp"
#include "mafia/messages.hpp"
#include "mafia/player.hpp"
#include "mafia/shared_ptr.hpp"

namespace mafia {

class PlayerRuntime {
public:
    virtual ~PlayerRuntime() = default;

    // Запускает игроков; players[i] — игрок с номером i + 1.
    virtual void start(const std::vector<SharedPtr<Player>>& players) = 0;
    virtual void send(int player, Request request) = 0;
    // Следующий ответ любого игрока (в порядке готовности).
    virtual Response receive() = 0;
    // Останавливает всех игроков; повторный вызов ничего не делает.
    virtual void stop() noexcept = 0;
};

// Каждый игрок — отдельная std::thread с блокирующими каналами.
class ThreadRuntime final : public PlayerRuntime {
public:
    ThreadRuntime();
    ~ThreadRuntime() override;

    void start(const std::vector<SharedPtr<Player>>& players) override;
    void send(int player, Request request) override;
    Response receive() override;
    void stop() noexcept override;

private:
    std::vector<SharedPtr<Player>> players_;
    std::vector<SharedPtr<RequestChannel>> inboxes_;
    SharedPtr<ResponseChannel> responses_;
    bool running_ = false;
};

// Каждый игрок — корутина Player::play(); все они живут в нити ведущего.
// receive() пошагово возобновляет готовые корутины, пока не появится ответ.
// Ввод человека читается, только когда продвигать больше некого: к этому
// моменту все боты уже ответили, и ожидание человека их не задерживает.
class CoroutineRuntime final : public PlayerRuntime {
public:
    explicit CoroutineRuntime(coro::AsyncInput::Reader readLine);
    ~CoroutineRuntime() override;

    void start(const std::vector<SharedPtr<Player>>& players) override;
    void send(int player, Request request) override;
    Response receive() override;
    void stop() noexcept override;

    // Асинхронный ввод для игрока-человека.
    const SharedPtr<coro::AsyncInput>& input() const noexcept { return input_; }
    // Сколько шагов (возобновлений корутин) сделал ведущий — для тестов.
    std::uint64_t steps() const noexcept { return steps_; }

    static coro::AsyncInput::Reader readerFor(std::istream& in);

private:
    void checkTasks() const;

    SharedPtr<coro::Scheduler> scheduler_;
    SharedPtr<coro::AsyncInput> input_;
    std::vector<SharedPtr<RequestCoChannel>> inboxes_;
    SharedPtr<ResponseCoChannel> responses_;
    std::vector<coro::Task<>> tasks_;
    std::uint64_t steps_ = 0;
    bool running_ = false;
};

}  // namespace mafia
