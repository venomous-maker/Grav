#ifndef GRAV_DIAGNOSTICS_H
#define GRAV_DIAGNOSTICS_H

#include <stdexcept>
#include <string>

namespace grav {

// Thrown by any stage of the pipeline when the source is invalid.
// Carries a 1-based source location so the driver can print a useful message.
class GravError : public std::runtime_error {
public:
    GravError(std::string stage, int line, int col, const std::string &message)
        : std::runtime_error(format(stage, line, col, message)),
          stage_(std::move(stage)), line_(line), col_(col) {}

    int line() const { return line_; }
    int col() const { return col_; }
    const std::string &stage() const { return stage_; }

private:
    static std::string format(const std::string &stage, int line, int col,
                              const std::string &message) {
        std::string label = (stage == "warning") ? "warning" : stage + " error";
        return label + " [" + std::to_string(line) + ":" +
               std::to_string(col) + "]: " + message;
    }

    std::string stage_;
    int line_;
    int col_;
};

} // namespace grav

#endif // GRAV_DIAGNOSTICS_H
