#ifndef GRAV_CODEGEN_FOLD_H
#define GRAV_CODEGEN_FOLD_H

#include <string>

namespace grav {

// Folds the methods of generic instances that compile to the same code. A generic
// class is instantiated once per type argument (Array<User>, Array<Order>, ...),
// and for class type arguments most of its methods differ only in the C types of
// the pointers they handle: the same machine code, written once per instance.
// Here each such method is kept once; the other instances' copies become one-line
// wrappers that call it (the C compiler inlines them).
//
// It works on the text. A method `<Instance>_m_<name>` takes part when its
// instance's name holds class type arguments (its "leaves", at any depth). Its
// body is compared with the other instances' after each leaf is replaced by a
// placeholder, but only where the name is a pointer type, the instance's own
// function or vtable type, or a generic instance's function: data (type info,
// method tables) and the class's own methods keep their names, so a body that
// uses them is never folded. Bodies fold when they are the same, their instances'
// struct layouts are the same (any struct pointer counting as a pointer), and the
// methods they call fold together too. The kept body's stack-trace label shows
// the leaves as `*` (`std.collections.Array$*.push`).
// Returns the C unchanged when nothing folds.
std::string foldGenericInstances(const std::string &c);

// Writes a `#line` directive's file name only when it changes (`#line 12` keeps
// the current one): the same lines for the debugger and the C compiler's
// messages, in a fraction of the text (a large program repeats long paths
// hundreds of thousands of times).
std::string compactLineDirectives(const std::string &c);

} // namespace grav

#endif // GRAV_CODEGEN_FOLD_H
