// `grav self-update`: updates a grav that install.sh installed.
#ifndef GRAV_DRIVER_SELF_UPDATE_H
#define GRAV_DRIVER_SELF_UPDATE_H

#include <string>
#include <vector>

namespace grav {

// `grav self-update [--check] [--force] [--ref REF]`; returns the process exit
// status (on success it does not return: it becomes install.sh --update).
int runSelfUpdate(const std::vector<std::string> &args);

} // namespace grav

#endif // GRAV_DRIVER_SELF_UPDATE_H
