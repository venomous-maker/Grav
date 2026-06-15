#ifndef GRAV_CODEGEN_MANGLE_H
#define GRAV_CODEGEN_MANGLE_H

#include <string>

#include "common/types.h"

namespace grav {

// Maps a fully-qualified Grav name (e.g. "geometry.Circle") to a C identifier
// ("geometry__Circle"). Dots become double underscores.
std::string mangle(const std::string &fqName);

// C identifier helpers for the various generated symbols.
std::string structName(const std::string &classFq);   // geometry__Circle
std::string vtableType(const std::string &classFq);    // geometry__Circle_VT
std::string vtableInstance(const std::string &classFq);// geometry__Circle_vtable
std::string methodCName(const std::string &classFq, const std::string &method);
std::string ctorCName(const std::string &classFq);     // geometry__Circle_new
std::string funcCName(const std::string &fnFq);        // vf_geometry__area

// C type spelling for a resolved Grav type. Named types become pointers.
std::string cType(const TypeRef &t);

} // namespace grav

#endif // GRAV_CODEGEN_MANGLE_H
