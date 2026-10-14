#include "mafia/logger.hpp"

namespace mafia {

Logger::Logger(std::ostream& out, bool fullLog, bool deferSecrets)
    : out_(out), fullLog_(fullLog), deferSecrets_(deferSecrets) {}

void Logger::announce(const std::string& line) {
    std::lock_guard lock(mutex_);
    out_ << line << '\n';
}

void Logger::secret(const std::string& line) {
    if (!fullLog_) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (deferSecrets_) {
        deferred_.push_back(line);
    } else {
        out_ << "    [лог] " << line << '\n';
    }
}

void Logger::personal(const std::string& line) {
    std::lock_guard lock(mutex_);
    out_ << ">> " << line << '\n';
}

void Logger::inputPrompt() {
    std::lock_guard lock(mutex_);
    out_ << "> " << std::flush;
}

void Logger::flush() {
    std::lock_guard lock(mutex_);
    out_.flush();
}

void Logger::flushSecrets() {
    std::lock_guard lock(mutex_);
    if (deferred_.empty()) {
        return;
    }
    out_ << "\n---------- Полный лог скрытых действий ----------\n";
    for (const auto& line : deferred_) {
        out_ << "    [лог] " << line << '\n';
    }
    deferred_.clear();
}

}  // namespace mafia
