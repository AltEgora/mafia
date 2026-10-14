#pragma once

#include <mutex>
#include <ostream>
#include <string>
#include <vector>

namespace mafia {

// Единственная точка вывода. Пишет только ведущий; мьютекс — защита на случай,
// если вывод когда-нибудь понадобится из другой нити.
class Logger {
public:
    // deferSecrets: в интерактивной игре с --full-log тайные действия
    // показываются только в конце, чтобы человек не подсматривал.
    Logger(std::ostream& out, bool fullLog, bool deferSecrets);

    void announce(const std::string& line);  // публичные объявления ведущего — всегда
    void secret(const std::string& line);    // скрытые действия и рассуждения — только --full-log
    void personal(const std::string& line);  // подсказки и личные сообщения человеку
    void inputPrompt();                      // приглашение ко вводу без перевода строки
    void flushSecrets();
    void flush();

private:
    std::mutex mutex_;
    std::ostream& out_;
    const bool fullLog_;
    const bool deferSecrets_;
    std::vector<std::string> deferred_;
};

}  // namespace mafia
