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
          stage_(std::move(stage)), line_(line), col_(col), message_(message) {}

    int line() const { return line_; }
    int col() const { return col_; }
    const std::string &stage() const { return stage_; }
    const std::string &message() const { return message_; }

    // A warning's mechanical fix (`grav lint --fix`): replace the source from
    // (line, col) to just before (endLine, endCol) with `text`. Lines are the
    // combined program's, as line() is.
    struct Fix { int line = 0, col = 0, endLine = 0, endCol = 0; std::string text; };
    void setFix(Fix f) { fix_ = std::move(f); hasFix_ = true; }
    bool hasFix() const { return hasFix_; }
    const Fix &fix() const { return fix_; }

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
    std::string message_;
    Fix fix_;
    bool hasFix_ = false;
};

} // namespace grav

#endif // GRAV_DIAGNOSTICS_H
