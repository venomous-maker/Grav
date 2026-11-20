// `grav fmt`: indentation and whitespace normalization for Grav source.
#ifndef GRAV_DRIVER_FORMAT_H
#define GRAV_DRIVER_FORMAT_H

#include <string>
#include <vector>

namespace grav {

std::string formatGrav(const std::string &source);
// The first pass of formatGrav: spacing within lines, a statement per line in blocks.
std::string layoutGrav(const std::string &source);
// `grav fmt [--check] [paths...]`; returns the process exit status.
int runFormat(const std::vector<std::string> &args);

} // namespace grav

#endif // GRAV_DRIVER_FORMAT_H
