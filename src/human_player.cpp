#include "mafia/human_player.hpp"

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mafia/player_concepts.hpp"
#include "mafia/ranges_util.hpp"

namespace mafia {

namespace {

bool parseNumber(const std::string& text, int& value) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);
    return error == std::errc{} && ptr == end;
}

bool oneOf(std::string_view word, std::initializer_list<std::string_view> options) {
    return contains(options, word);
}

Response failure(std::string message) {
    Response response;
    response.valid = false;
    response.error = std::move(message);
    return response;
}

}  // namespace

HumanPlayer::HumanPlayer(int id, Role role, std::istream& input) : Player(id, role), input_(input) {}

Response HumanPlayer::discuss(const Request& request) {
    return readResponse(request);
}

Response HumanPlayer::vote(const Request& request) {
    return readResponse(request);
}

Response HumanPlayer::act(const Request& request) {
    return readResponse(request);
}

// Человек может получить любую роль, поэтому у него есть act(): он NightPlayer,
// и playTurn выбирает перегрузку с ночным ходом. Будит ли его ведущий ночью,
// решает роль (visitNightRole).
Response HumanPlayer::handle(const Request& request) {
    return playTurn(*this, request);
}

void HumanPlayer::useAsyncInput(SharedPtr<coro::AsyncInput> input) {
    asyncInput_ = std::move(input);
}

// Корутина засыпает до строки ввода, затем ход идёт обычным путём (playTurn).
coro::Task<Response> HumanPlayer::respondAsync(Request request) {
    if (asyncInput_) {
        pendingLine_ = co_await asyncInput_->nextLine();
    }
    co_return handle(request);
}

std::optional<std::string> HumanPlayer::takeLine() {
    if (pendingLine_) {
        return *std::exchange(pendingLine_, std::nullopt);
    }
    std::string line;
    if (!std::getline(input_, line)) {
        return std::nullopt;
    }
    return line;
}

Response HumanPlayer::readResponse(const Request& request) {
    const std::optional<std::string> nextLine = takeLine();
    if (!nextLine) {
        Response response = failure("ввод закончился");
        response.endOfInput = true;
        return response;
    }

    std::istringstream stream(*nextLine);
    std::vector<std::string> words;
    for (std::string word; stream >> word;) {
        words.push_back(word);
    }
    if (words.empty()) {
        return failure("пустая строка");
    }

    const auto available = [&request](int player) { return contains(request.targets, player); };

    // Журналист: два разных номера через пробел.
    if (request.type == RequestType::Night && role() == Role::Journalist) {
        int first = 0;
        int second = 0;
        if (words.size() != 2 || !parseNumber(words[0], first) || !parseNumber(words[1], second)) {
            return failure("нужно два номера через пробел, например «2 5»");
        }
        if (first == second) {
            return failure("сравнивать нужно двух разных игроков");
        }
        for (int player : {first, second}) {
            if (!available(player)) {
                return failure("игрок " + std::to_string(player) + " недоступен для сравнения");
            }
        }
        Response response;
        response.action = ActionType::Compare;
        response.target = first;
        response.secondTarget = second;
        return response;
    }

    ActionType action = ActionType::None;
    std::string numberText;
    if (request.type == RequestType::Night && role() == Role::Commissar) {
        if (words.size() != 2) {
            return failure("нужно два слова: «п N» или «в N»");
        }
        if (oneOf(words[0], {"п", "П", "p", "c", "check"})) {
            action = ActionType::Check;
        } else if (oneOf(words[0], {"в", "В", "s", "shoot"})) {
            action = ActionType::Shoot;
        } else {
            return failure("первое слово должно быть «п» (проверить) или «в» (выстрелить)");
        }
        numberText = words[1];
    } else {
        if (words.size() != 1) {
            return failure("нужен один номер игрока");
        }
        numberText = words[0];
    }

    int target = 0;
    if (!parseNumber(numberText, target)) {
        return failure("«" + numberText + "» — не номер игрока");
    }

    switch (request.type) {
        case RequestType::Discussion:
            action = target == 0 ? ActionType::ClaimInnocent : ActionType::Accuse;
            break;
        case RequestType::DayVote:
            action = ActionType::Vote;
            break;
        case RequestType::Night:
            if (role() == Role::Doctor) {
                action = ActionType::Heal;
            } else if (role() == Role::Bodyguard) {
                action = ActionType::Guard;
            } else if (role() != Role::Commissar) {
                action = ActionType::Kill;  // мафия, ниндзя, маньяк
            }
            break;
        default:
            break;
    }

    const bool needsTarget = action != ActionType::ClaimInnocent;
    if (needsTarget && !available(target)) {
        return failure("игрок " + numberText + " недоступен для этого действия");
    }

    Response response;
    response.action = action;
    response.target = needsTarget ? target : 0;
    return response;
}

}  // namespace mafia
