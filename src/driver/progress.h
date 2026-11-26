#ifndef GRAV_DRIVER_PROGRESS_H
#define GRAV_DRIVER_PROGRESS_H

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace grav {

// What a build is doing, for a person watching a terminal. A large program spends a
// minute or two in each stage, and without a word it looks stuck. A stage is
// announced only once it has run for a couple of seconds, so a small program's
// build prints nothing extra. Off unless stderr is a terminal.
class Progress {
public:
    explicit Progress(bool on);
    ~Progress();
    Progress(const Progress &) = delete;
    Progress &operator=(const Progress &) = delete;

    // The stage now running ("turning main.grav into C").
    void stage(const std::string &what);
    // The stage is over and nothing more will be announced.
    void stop();
    // How long since the build started, as "1m 05s", when that is long enough to
    // be worth saying (and anything was announced); otherwise "".
    std::string elapsed() const;

private:
    void watch();

    bool on_;
    std::chrono::steady_clock::time_point start_, stageStart_;
    std::string stage_;
    bool shown_ = true, done_ = false, spoke_ = false;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::thread thread_;
};

} // namespace grav

#endif // GRAV_DRIVER_PROGRESS_H
