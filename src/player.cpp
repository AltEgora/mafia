#include "mafia/player.hpp"

#include <exception>
#include <utility>

namespace mafia {

Player::Player(int id, Role role) : id_(id), role_(role) {}

Player::~Player() {
    // Штатно нить останавливает ведущий (Moderator::shutdown). Это страховка,
    // чтобы деструктор std::thread не вызвал std::terminate.
    if (thread_.joinable()) {
        inbox_->push(Request{.type = RequestType::Stop});
        thread_.join();
    }
}

void Player::start(SharedPtr<RequestChannel> inbox, SharedPtr<ResponseChannel> outbox) {
    inbox_ = std::move(inbox);
    outbox_ = std::move(outbox);
    thread_ = std::thread(&Player::run, this);
}

void Player::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Player::onInfo(const Request&) {}

coro::Task<Response> Player::respondAsync(Request request) {
    co_return handle(request);
}

coro::Task<> Player::play(SharedPtr<RequestCoChannel> inbox, SharedPtr<ResponseCoChannel> outbox) {
    for (;;) {
        Request request = co_await inbox->receive();
        if (request.type == RequestType::Stop) {
            co_return;
        }
        if (request.type == RequestType::Info) {
            onInfo(request);
            continue;
        }
        Response response;
        try {
            response = co_await respondAsync(request);
        } catch (const std::exception& error) {
            response = Response{.valid = false, .error = error.what()};
        }
        response.player = id_;
        response.seq = request.seq;
        outbox->push(std::move(response));
    }
}

void Player::run() {
    for (;;) {
        Request request = inbox_->pop();
        if (request.type == RequestType::Stop) {
            return;
        }
        if (request.type == RequestType::Info) {
            onInfo(request);
            continue;
        }
        // Ведущий ждёт ответа на каждый запрос, поэтому исключение
        // превращаем в некорректный ответ, а не теряем.
        Response response;
        try {
            response = handle(request);
        } catch (const std::exception& error) {
            response = Response{.valid = false, .error = error.what()};
        }
        response.player = id_;
        response.seq = request.seq;
        outbox_->push(std::move(response));
    }
}

}  // namespace mafia
