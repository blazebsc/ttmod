#include "ttmod/log.hpp"
#include "ttmod/file_io.hpp"
#include <cstdio>
#include <mutex>

namespace ttmod {

static std::mutex& mtx() { static std::mutex m; return m; }

bool Logger::open(const std::string& path) {
    std::lock_guard<std::mutex> l(mtx());
    close();
    fp_ = ttmod::file_io::open_append(path);
    return fp_ != nullptr;
}

void Logger::write(const char* level, const std::string& msg) {
    std::lock_guard<std::mutex> l(mtx());
    if (!fp_) return;
    FILE* f = (FILE*)fp_;
    fprintf(f, "[%s] %s\n", level, msg.c_str());
    fflush(f);
}

void Logger::info(const std::string& msg) { write("INFO", msg); }
void Logger::warn(const std::string& msg) { write("WARN", msg); }
void Logger::error(const std::string& msg) { write("ERROR", msg); }

void Logger::close() {
    if (fp_) { fclose((FILE*)fp_); fp_ = nullptr; }
}

} // namespace ttmod
