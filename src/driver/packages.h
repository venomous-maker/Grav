// The package manager behind `grav new | add | install | update | remove | packages`.
#ifndef GRAV_DRIVER_PACKAGES_H
#define GRAV_DRIVER_PACKAGES_H

#include <string>
#include <vector>

namespace grav {

bool isPackageCommand(const std::string &cmd);
// `args[0]` is the command; returns the process exit status.
int runPackageCommand(const std::vector<std::string> &args);

} // namespace grav

#endif // GRAV_DRIVER_PACKAGES_H
