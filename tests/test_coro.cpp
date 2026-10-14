// Тесты корутин: Task, планировщик, канал, асинхронный ввод и режим игроков-корутин.

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "mafia/coro.hpp"
#include "mafia/human_player.hpp"
#include "mafia/player_runtime.hpp"
#include "test_framework.hpp"

using namespace mafia;
using coro::Task;

namespace {

Task<int> answer(bool& started) {
    started = true;
    co_return 42;
}

Task<int> fails() {
    throw std::runtime_error("ошибка внутри корутины");
    co_return 0;
}

Task<int> twice(bool& started) {
    const int value = co_await answer(started);
    co_return value * 2;
}

// Корневая задача: кладёт результат вложенной задачи в out.
Task<> root(Task<int> inner, std::optional<int>& out, std::string& error) {
    try {
        out = co_await std::move(inner);
    } catch (const std::exception& e) {
        error = e.what();
    }
}

// Игрок-заглушка: отвечает своим номером и запоминает, в какой нити думал.
class Echo final : public Player {
public:
    Echo(int id, std::vector<std::thread::id>& threads) : Player(id, Role::Civilian), threads_(threads) {}

protected:
    Response handle(const Request&) override {
        threads_.push_back(std::this_thread::get_id());
        Response response;
        response.action = ActionType::Vote;
        response.target = id();
        return response;
    }

private:
    std::vector<std::thread::id>& threads_;
};

SharedPtr<const GameView> view(int players) {
    auto snapshot = MakeShared<GameView>();
    for (int id = 1; id <= players; ++id) {
        snapshot->players.push_back(PlayerPublicInfo{.id = id});
    }
    return snapshot;
}

Request discussion(std::vector<int> targets) {
    return Request{.type = RequestType::Discussion, .view = view(4), .targets = std::move(targets)};
}

}  // namespace

TEST(TaskIsLazyAndReturnsValue) {
    bool started = false;
    std::optional<int> result;
    std::string error;
    Task<> task = root(twice(started), result, error);
    CHECK(!started);  // ленивая: тело не выполняется до запуска
    CHECK(!task.done());
    task.handle().resume();
    CHECK(started);
    CHECK(task.done());
    CHECK(result == 84);  // вложенный co_await и симметричная передача управления
    CHECK(error.empty());
}

TEST(TaskPropagatesExceptions) {
    std::optional<int> result;
    std::string error;
    Task<> task = root(fails(), result, error);
    task.handle().resume();
    CHECK(task.done());
    CHECK(!result);
    CHECK_EQ(error, std::string("ошибка внутри корутины"));
}

TEST(ChannelSuspendsAndResumesReceiver) {
    auto scheduler = MakeShared<coro::Scheduler>();
    auto channel = MakeShared<coro::CoChannel<int>>(scheduler);
    std::vector<int> received;
    auto consumer = [](SharedPtr<coro::CoChannel<int>> in, std::vector<int>& out) -> Task<> {
        for (;;) {
            const int value = co_await in->receive();
            if (value < 0) {
                co_return;
            }
            out.push_back(value);
        }
    };
    Task<> task = consumer(channel, received);
    scheduler->schedule(task.handle());
    scheduler->runUntilIdle();
    CHECK(received.empty());  // очередь пуста — получатель спит
    CHECK(!scheduler->hasReady());

    channel->push(1);
    channel->push(2);
    CHECK(scheduler->hasReady());  // push разбудил получателя
    scheduler->runUntilIdle();
    CHECK(received == (std::vector<int>{1, 2}));

    channel->push(-1);
    scheduler->runUntilIdle();
    CHECK(task.done());
}

TEST(AsyncInputDeliversLinesOnDemand) {
    auto scheduler = MakeShared<coro::Scheduler>();
    std::vector<std::optional<std::string>> lines = {std::string("первая"), std::nullopt};
    std::size_t reads = 0;
    auto input = MakeShared<coro::AsyncInput>(scheduler, [&]() { return lines[reads++]; });
    std::vector<std::optional<std::string>> got;
    auto reader = [](SharedPtr<coro::AsyncInput> in, std::vector<std::optional<std::string>>& out) -> Task<> {
        out.push_back(co_await in->nextLine());
        out.push_back(co_await in->nextLine());
    };
    Task<> task = reader(input, got);
    scheduler->schedule(task.handle());
    scheduler->runUntilIdle();
    CHECK(input->waiting());
    CHECK_EQ(reads, std::size_t{0});  // строка читается только по запросу ведущего
    input->deliver();
    scheduler->runUntilIdle();
    input->deliver();
    scheduler->runUntilIdle();
    CHECK(task.done());
    CHECK(got.size() == 2 && got[0] == std::string("первая") && !got[1]);
}

// Все игроки-корутины работают в нити ведущего, без отдельных std::thread.
TEST(CoroutinePlayersRunInModeratorThread) {
    std::vector<std::thread::id> threads;
    std::vector<SharedPtr<Player>> players;
    for (int id = 1; id <= 4; ++id) {
        players.push_back(MakeShared<Echo>(id, threads));
    }
    CoroutineRuntime runtime(CoroutineRuntime::readerFor(std::cin));
    runtime.start(players);
    for (int id = 1; id <= 4; ++id) {
        runtime.send(id, discussion({1, 2, 3, 4}));
    }
    std::vector<int> answered;
    for (int i = 0; i < 4; ++i) {
        const Response response = runtime.receive();
        CHECK_EQ(response.target, response.player);
        answered.push_back(response.player);
    }
    std::ranges::sort(answered);
    CHECK(answered == (std::vector<int>{1, 2, 3, 4}));
    CHECK_EQ(threads.size(), std::size_t{4});
    for (const auto& thread : threads) {
        CHECK(thread == std::this_thread::get_id());
    }
    CHECK(runtime.steps() > 0);
    runtime.stop();
}

// Человек ждёт ввода, но боты отвечают раньше, чем ведущий вообще читает строку.
TEST(HumanDoesNotBlockBots) {
    std::vector<std::thread::id> threads;
    std::size_t reads = 0;
    std::size_t botAnswersBeforeRead = 0;
    std::size_t botAnswers = 0;
    CoroutineRuntime runtime([&]() -> std::optional<std::string> {
        ++reads;
        botAnswersBeforeRead = botAnswers;
        return std::string("0");  // «я мирный житель»
    });
    auto human = MakeShared<HumanPlayer>(1, Role::Civilian, std::cin);
    human->useAsyncInput(runtime.input());
    std::vector<SharedPtr<Player>> players = {human};
    for (int id = 2; id <= 4; ++id) {
        players.push_back(MakeShared<Echo>(id, threads));
    }
    runtime.start(players);
    for (int id = 1; id <= 4; ++id) {  // человеку — первым
        runtime.send(id, discussion({1, 2, 3, 4}));
    }
    for (int i = 0; i < 3; ++i) {
        const Response response = runtime.receive();
        CHECK(response.player != 1);
        ++botAnswers;
    }
    CHECK_EQ(reads, std::size_t{0});
    const Response humanAnswer = runtime.receive();
    CHECK_EQ(humanAnswer.player, 1);
    CHECK(humanAnswer.action == ActionType::ClaimInnocent);
    CHECK_EQ(reads, std::size_t{1});
    CHECK_EQ(botAnswersBeforeRead, std::size_t{3});
    runtime.stop();
}

TEST(EndOfInputReachesModerator) {
    CoroutineRuntime runtime([]() -> std::optional<std::string> { return std::nullopt; });
    auto human = MakeShared<HumanPlayer>(1, Role::Doctor, std::cin);
    human->useAsyncInput(runtime.input());
    runtime.start({human});
    runtime.send(1, discussion({2, 3}));
    const Response response = runtime.receive();
    CHECK(response.endOfInput);
    CHECK(!response.valid);
    runtime.stop();
}

TEST(ReceiveWithoutPendingRequestsIsAnError) {
    std::vector<std::thread::id> threads;
    CoroutineRuntime runtime(CoroutineRuntime::readerFor(std::cin));
    runtime.start({MakeShared<Echo>(1, threads)});
    CHECK_THROWS_WITH(runtime.receive(), std::logic_error, "ответа нет");
    runtime.stop();
    runtime.stop();  // повторная остановка ничего не делает
}

MAFIA_TEST_MAIN()
