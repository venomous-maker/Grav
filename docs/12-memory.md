# 12. Memory

Grav manages memory with automatic reference counting (ARC). There is no garbage
collector and no pause: every object counts the references to it, and when the last
one goes away the destructor runs and the memory is freed, right then. Strings
built at run time are counted the same way (see below).

## Objects are freed when the last reference goes

Local variables, fields, globals, closures and collections each hold a reference.
When a variable goes out of scope, is reassigned, or the object holding it is
destroyed, its reference is dropped:

```grav
import "std"

class Resource {
    protected name: string
    constructor(name: string) { self.name = name; print("open " + name) }
    destructor() { print("close " + self.name) }
}

fn main() {
    if (true) {
        let a = new Resource("a")
        let b = new Resource("b")
    }                                  // both closed here, b first
    let x = new Resource("x1")
    x = new Resource("x2")             // x1 closed: nothing refers to it any more
    new Resource("temp")               // a result nobody keeps: closed after the statement
    let items = new Array<Resource>()
    items.push(new Resource("kept"))   // the array keeps it alive
    print("end of main")
}                                      // items, x2 and kept are closed on the way out
```

Output:

```text
open a
open b
close b
close a
open x1
open x2
close x1
open temp
close temp
open kept
end of main
close kept
close x2
```

- A result used only inside an expression (`makeThing().name`) lives until the
  end of that statement.
- Counts are atomic, so objects can be shared between threads; a running `Thread`
  keeps its own object alive.
- Long chains (a million-node linked list) are freed iteratively, without deep
  recursion.

## Cycles and weak fields

Two objects that refer to each other, such as a parent whose `children` point back
at it, would keep each other alive forever. Mark the back-reference `weak`: a weak
field does not keep its object alive, and reads as `null` once that object is gone.

```grav
import "std"

class TreeNode {
    public name: string
    public children: Array<TreeNode>
    public weak parent: TreeNode
    constructor(name: string) { self.name = name; self.children = new Array<TreeNode>(); self.parent = null }
    destructor() { print("free " + self.name) }
    fn add(child: TreeNode) -> TreeNode { child.parent = self; self.children.push(child); return child }
}

fn main() {
    let leaf: TreeNode = null
    if (true) {
        let root = new TreeNode("root")
        leaf = root.add(new TreeNode("leaf"))
        print("parent: " + leaf.parent.name)
    }                                  // root goes: nothing strong refers to it
    print("parent gone: ${leaf.parent == null}")
}
```

Output:

```text
parent: root
free root
parent gone: true
free leaf
```

- `weak` applies to fields of class type. Reading one gives the object for the
  rest of the statement, or `null`; store it in a local to keep it longer.
- An object disposed with `free` also reads as `null` through weak fields.
- Without `weak`, break a cycle by hand: set one side to `null`, or call `free` on
  one of the objects, when you are done.

## free

`free(obj)` runs the destructor chain now, the dynamic type's destructor and then
each base class's, and releases the object's fields (object fields become `null`,
strings `""`). The memory itself stays until the last reference is gone, so
another variable still pointing at the object never dangles. You rarely need it now; it is useful to close a resource
deterministically while other references may remain.

```grav
class Database {
    public name: string
    constructor(name: string) { self.name = name }
    destructor() { print("flush ${self.name}") }
}

fn main() {
    let db = new Database("main")
    let alias = db
    free(db)                           // flushed now, once
    print("still safe to read: [" + alias.name + "]")
}
```

Output:

```text
flush main
still safe to read: []
```

## defer for guaranteed cleanup

```grav
class File {
    private path: string
    constructor(path: string) { self.path = path; print("open ${path}") }
    destructor() { print("close ${self.path}") }
}

fn process(path: string, fail: bool) -> string {
    let f = new File(path)
    defer free(f)                        // runs on every way out
    if (fail) { return "failed" }
    return "ok"
}

fn main() {
    print(process("a.txt", false))
    print(process("b.txt", true))
}
```

Output:

```text
open a.txt
close a.txt
ok
open b.txt
close b.txt
failed
```

## Strings

Strings built at run time (by `+`, interpolation, `str()` and the string
functions) are reference counted like objects, wherever they are kept: locals,
fields, collections, closures. Text is freed as soon as nothing refers to it.
Literals are permanent, and copying a string only adds a reference.

```grav
import "std"

class Log {
    public lines: Array<string>
    constructor() { self.lines = new Array<string>() }
}

fn main() {
    let report = ""
    let log = new Log()
    for (i in 0..1000) {
        let line = "item ${i}"           // freed at the end of the iteration...
        if (i % 250 == 0) {
            report += line + "; "        // ...unless kept; the old report is freed
            log.lines.push(line)
        }
    }
    print("report: " + report)
    print("kept ${log.lines.size()} lines")
}                                        // the log and its lines are freed here
```

Output:

```text
report: item 0; item 250; item 500; item 750; 
kept 4 lines
```

C code that makes strings for Grav (in `%{ … %}` blocks) allocates them with
`grav_str_new(len)` (or `grav_str_alloc(bytes)`); strings from `malloc`, static
buffers and C libraries are treated as permanent and never freed by Grav.

## StringBuilder

For building large text, `StringBuilder` appends into one growing buffer instead of
copying the whole string on every `+=`:

```grav
import "../lib/std.grav"

fn main() {
    let sb = new StringBuilder()
    for (i in 0..5) { sb.append("#").appendInt(i).append(" ") }
    sb.appendLine("done")
    print(sb)                            // toString() makes it printable
    free(sb)
}
```

Output:

```text
#0 #1 #2 #3 #4 done

```

## Closures

A closure keeps the variables it captures alive (objects included) for as long as
the closure itself exists; closures are reference counted like objects.

## Pointers and C memory

Pointers obtained from inline C (for example a `malloc`ed buffer) are released with
`free(p)` as in C. The `Array<T>` implementation in `lib/array.grav` is a good
example of managing raw memory from Grav.

Next: [Modules, namespaces & C interop →](13-modules.md)
