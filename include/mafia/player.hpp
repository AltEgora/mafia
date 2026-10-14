#pragma once

#include <thread>

#include "mafia/channel.hpp"
#include "mafia/coro.hpp"
#include "mafia/messages.hpp"
#include "mafia/roles.hpp"
#include "mafia/shared_ptr.hpp"

namespace mafia {

using RequestChannel = Channel<Request>;
using ResponseChannel = Channel<Response>;
using RequestCoChannel = coro::CoChannel<Request>;
using ResponseCoChannel = coro::CoChannel<Response>;

// Базовый класс игрока. Игрок ждёт запрос ведущего, принимает решение и
// отправляет ответ — в одном из двух режимов:
//   * нить (по умолчанию): start() запускает std::thread с циклом run();
//   * корутина (--coroutines): play() — корутина с тем же циклом, её пошагово
//     возобновляет ведущий в своей нити.
// Игрок ничего не печатает — весь вывод идёт через ведущего.
class Player {
public:
    Player(int id, Role role);
    virtual ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    int id() const noexcept { return id_; }
    Role role() const noexcept { return role_; }

    // Режим нитей. Вся настройка игрока должна быть сделана до start().
    void start(SharedPtr<RequestChannel> inbox, SharedPtr<ResponseChannel> outbox);
    void join();

    // Режим корутин: тот же цикл «запрос → ответ», что и run(), но вместо
    // блокировок корутина засыпает на co_await.
    coro::Task<> play(SharedPtr<RequestCoChannel> inbox, SharedPtr<ResponseCoChannel> outbox);

    // Передать личное сообщение ведущего (Info) напрямую — так ИИ-агент делится
    // им со своим вложенным ботом. Вызывать только из нити (корутины) игрока.
    void deliverInfo(const Request& info) { onInfo(info); }

protected:
    // Вызываются только из нити (или корутины) игрока.
    virtual Response handle(const Request& request) = 0;
    virtual void onInfo(const Request& request);

    // Ответ в режиме корутин. По умолчанию — сразу handle(); человек
    // переопределяет, чтобы дождаться строки ввода, не задерживая остальных.
    virtual coro::Task<Response> respondAsync(Request request);

private:
    void run();

    const int id_;
    const Role role_;
    SharedPtr<RequestChannel> inbox_;
    SharedPtr<ResponseChannel> outbox_;
    std::thread thread_;
};

}  // namespace mafia
