#include "driver/progress.h"

#include <cstdio>

namespace grav {

namespace {
constexpr auto kQuiet = std::chrono::seconds(2);  // a stage shorter than this is not announced
}

Progress::Progress(bool on) : on_(on), start_(std::chrono::steady_clock::now()), stageStart_(start_) {
    if (on_) thread_ = std::thread([this] { watch(); });
}

Progress::~Progress() { stop(); }

void Progress::stage(const std::string &what) {
    if (!on_) return;
    std::lock_guard<std::mutex> lock(mu_);
    stage_ = what;
    stageStart_ = std::chrono::steady_clock::now();
    shown_ = false;
    cv_.notify_all();
}

void Progress::stop() {
    if (!on_) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (done_) return;
        done_ = true;
        cv_.notify_all();
    }
    if (thread_.joinable()) thread_.join();
}

std::string Progress::elapsed() const {
    if (!on_) return "";
    std::lock_guard<std::mutex> lock(mu_);
    if (!spoke_) return "";
    long s = (long)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_).count();
    char buf[32];
    if (s >= 60) std::snprintf(buf, sizeof buf, "%ldm %02lds", s / 60, s % 60);
    else std::snprintf(buf, sizeof buf, "%lds", s);
    return buf;
}

void Progress::watch() {
    std::unique_lock<std::mutex> lock(mu_);
    while (!done_) {
        if (!shown_) {
            auto due = stageStart_ + kQuiet;
            if (std::chrono::steady_clock::now() >= due) {
                std::fprintf(stderr, "gravc: %s...\n", stage_.c_str());
                std::fflush(stderr);
                shown_ = spoke_ = true;
                continue;
            }
            cv_.wait_until(lock, due);
        } else {
            cv_.wait(lock);
        }
    }
}

} // namespace grav
