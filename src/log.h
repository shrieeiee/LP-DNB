// Tiny thread-safe log: printed to stdout and kept for the GUI's status area.
#pragma once
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace dnb {

class Log {
public:
    static Log& get() {
        static Log l;
        return l;
    }
    // The window version has no console; writing to a stdout nobody reads can block.
    void setEcho(bool on) { echo_ = on; }
    void add(const std::string& s) {
        if (echo_) {
            std::printf("%s\n", s.c_str());
            std::fflush(stdout);
        }
        std::lock_guard<std::mutex> lk(mu_);
        lines_.push_back(s);
        while (lines_.size() > 200) lines_.pop_front();
        ++serial_;
    }
    std::vector<std::string> tail(size_t n) {
        std::lock_guard<std::mutex> lk(mu_);
        size_t start = lines_.size() > n ? lines_.size() - n : 0;
        return std::vector<std::string>(lines_.begin() + static_cast<long>(start), lines_.end());
    }
    unsigned serial() {
        std::lock_guard<std::mutex> lk(mu_);
        return serial_;
    }

private:
    std::mutex mu_;
    std::deque<std::string> lines_;
    unsigned serial_ = 0;
    bool echo_ = true;
};

inline void logf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    Log::get().add(buf);
}

}  // namespace dnb
