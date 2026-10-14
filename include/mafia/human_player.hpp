#pragma once

#include <istream>
#include <optional>
#include <string>

#include "mafia/player.hpp"

namespace mafia {

// Игрок-человек. Его нить (или корутина) только получает строку ввода и
// разбирает её в ответ; подсказки и сообщения об ошибках печатает ведущий.
// В режиме нитей строка читается из потока прямо в нити игрока. В режиме
// корутин корутина засыпает на AsyncInput, а строку читает ведущий, когда
// все боты уже ответили, — ожидание человека никого не задерживает.
class HumanPlayer final : public Player {
public:
    HumanPlayer(int id, Role role, std::istream& input);

    // Интерфейс участника дня и ночи (см. player_concepts.hpp); каждый метод
    // читает одну строку ввода и разбирает её под текущий запрос.
    Response discuss(const Request& request);
    Response vote(const Request& request);
    Response act(const Request& request);

    // Режим корутин: строки приходят через асинхронный ввод ведущего.
    void useAsyncInput(SharedPtr<coro::AsyncInput> input);

protected:
    Response handle(const Request& request) override;
    coro::Task<Response> respondAsync(Request request) override;

private:
    Response readResponse(const Request& request);
    std::optional<std::string> takeLine();

    std::istream& input_;
    SharedPtr<coro::AsyncInput> asyncInput_;
    std::optional<std::optional<std::string>> pendingLine_;  // строка, полученная корутиной
};

}  // namespace mafia
