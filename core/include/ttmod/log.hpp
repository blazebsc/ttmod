#pragma once
#include <string>

namespace ttmod {

// Minimal append logger. Portable; Windows DLL and Linux tools share it.
class Logger {
public:
    bool open(const std::string& path);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);
    void close();
    bool is_open() const { return fp_ != nullptr; }
    ~Logger() { close(); }
private:
    void write(const char* level, const std::string& msg);
    void* fp_ = nullptr; // FILE*, pimpl without <cstdio> in header
};

} // namespace ttmod
