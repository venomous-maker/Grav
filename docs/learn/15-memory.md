# 15. Memory

Every object, string and list your program creates takes up memory, and that
memory must be given back when the value is no longer needed. In C you do this by
hand, and forgetting (a leak) or doing it too early (a crash) are classic bugs.
Many languages use a *garbage collector* that pauses the program now and then to
find unused memory. Grav does neither: it counts references.

In this chapter you will learn:

- where values live, and which ones need managing,
- automatic reference counting: when exactly objects are freed,
- reference cycles, the cycle collector, and `weak` fields,
- `free` and `defer` for closing resources at a precise moment,
- how strings and closures are managed.

## Values and objects

Some values live directly in their variable and need no management at all:
numbers, booleans, enums, structs of those, and fixed arrays of those. When the
variable goes away, so does the value.

Everything that can be shared lives in a separate block of memory: objects
created with `new`, collections, strings built while the program runs, closures,
and data enums. A variable holds a *reference* to the block. Several variables,
fields and collections can refer to the same block, and the question is when
nobody refers to it any more.

## Reference counting

Each shared block keeps a count of the references to it. Storing a reference in a
variable, field or collection adds one; when a variable ends, is reassigned, or
the object holding the reference is destroyed, the count goes down. When it reaches
zero, the object's destructor runs and its memory is freed, **immediately**:

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
    new Resource("temp")               // nobody keeps it: closed after the statement
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

This is *automatic reference counting* (ARC), the same approach Swift uses. What
it gives you:

- **No pauses.** There is no collector stopping the program to look for garbage.
- **Predictable clean-up.** A destructor runs at a known point, so it can close a
  file or a socket reliably.
- **Low memory use.** Memory is reused as soon as it is free.

A few details:

- A temporary used only inside an expression (`makeThing().name`) lives until
  the end of the statement.
- Counts are updated atomically, so objects can be shared between threads.
- Long chains, like a linked list with a million nodes, are freed without deep
  recursion.
- An exception releases the variables of every function it leaves on its way
  to a `catch`, so a failed call holds on to nothing. A constructor that throws
  frees its half-built object without running its destructor.

You do not write any of this: the compiler inserts the counting. Most of the
time you can forget memory exists.

## Cycles

Reference counting has one blind spot. If two objects refer to each other, each
keeps the other's count above zero, even when nothing else refers to either:

```text
   a ──other──▶ b
   ▲            │
   └───other────┘      nobody else points here, but both counts are 1
```

Grav has a *cycle collector* for this case. It finds groups of objects that
only refer to each other, runs their destructors and frees them. It runs by
itself as such candidates pile up, and `GC.collect()` runs it at once, returning
how many objects it freed:

```grav
import "std"

class Peer {
    public name: string
    public other: Peer
    constructor(name: string) { self.name = name; self.other = null }
    destructor() { print("free " + self.name) }
}

fn main() {
    if (true) {
        let a = new Peer("a")
        let b = new Peer("b")
        a.other = b
        b.other = a
    }
    print("collected ${GC.collect()}")
}
```

Output:

```text
free b
free a
collected 2
```

The collector only runs while no other threads or async tasks are working with
objects, and the destructors of a cycle run in no particular order. When you know
which reference "points back", you can avoid the cycle altogether.

### Weak fields

A field marked `weak` refers to an object without keeping it alive. When the
object is freed, the weak field reads as `null`. The classic use is a child's link
back to its parent:

```grav
import "std"

class TreeNode {
    public name: string
    public children: Array<TreeNode>
    public weak parent: TreeNode
    constructor(name: string) {
        self.name = name
        self.children = new Array<TreeNode>()
        self.parent = null
    }
    destructor() { print("free " + self.name) }
    fn add(child: TreeNode) -> TreeNode {
        child.parent = self
        self.children.push(child)
        return child
    }
}

fn main() {
    let leaf: TreeNode = null
    if (true) {
        let root = new TreeNode("root")
        leaf = root.add(new TreeNode("leaf"))
        print("parent: " + leaf.parent.name)
    }                                  // root goes: only weak links point to it
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

Parents own their children (strong references); children merely know their
parent (a weak reference). Same for an observer list, a cache, or an object that
holds a callback which captures the object itself: decide who owns whom, and make
the other direction `weak`.

`weak` works on fields of class type. Reading one gives you the object (or
`null`) for the rest of the statement; store it in a local variable to use it
longer.

## Closing things on time: free and defer

Usually the destructor runs when the last reference goes, and that is enough.
Sometimes you want to close a resource at a precise moment even though references
to it remain. `free(obj)` runs the destructor now, then the destructors of its
parent classes, and clears the object's fields:

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

The memory itself stays until the last reference is gone, so `alias` does not
point at freed memory; it just sees an emptied object (strings are `""`, objects
`null`). The destructor never runs twice.

`defer` and `free` together guarantee clean-up on every way out of a function:

```grav
class Handle {
    private path: string
    constructor(path: string) { self.path = path; print("open ${path}") }
    destructor() { print("close ${self.path}") }
}

fn process(path: string, fail: bool) -> string {
    let h = new Handle(path)
    defer free(h)
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

Strings are counted the same way as objects. Text built at run time (with `+`,
interpolation, `str()` or the `Str` functions) is freed as soon as nothing refers
to it. Literals are part of the program and never freed. Copying a string only
copies a reference, which is cheap, and safe because strings cannot change.

```grav
import "std"

fn main() {
    let report = ""
    let kept = new Array<string>()
    for (i in 0..1000) {
        let line = "item ${i}"          // freed at the end of each round...
        if (i % 250 == 0) {
            report += line + "; "       // ...unless kept somewhere
            kept.push(line)
        }
    }
    print("report: " + report)
    print("kept ${kept.size()} lines")
}
```

Output:

```text
report: item 0; item 250; item 500; item 750; 
kept 4 lines
```

Building a big string with `+=` in a loop copies it every time. For large text,
use a `StringBuilder` ([chapter 3](03-strings-and-text.md#building-long-strings)).

## Closures and structs

- A closure keeps the variables it captured alive, objects included, for as long
  as the closure exists. Closures are counted like objects.
- A struct or fixed array that contains strings or objects owns those
  references: copying it adds references, and they are released with the copy.
- A thrown exception is kept alive while it is in flight; the `catch` that
  handles it releases it at the end of the handler.

## Raw memory

Memory you allocate yourself in C (`malloc`, through inline C or a pointer) is
yours to free with `free(pointer)`, as in C. Grav never touches it. See
[C interop](17-c-interop.md).

## Finding memory bugs

Build with `--sanitize` to have AddressSanitizer check every memory access while
the program runs. It reports leaks and invalid accesses, typically in inline C or
pointer code, with the Grav line numbers. `--race` finds data races between
threads.

## Summary

- Numbers, enums and plain structs live in their variables; objects, collections,
  strings and closures are shared and reference counted.
- An object is freed, and its destructor run, the moment its last reference goes.
- Reference cycles are freed by the cycle collector (`GC.collect()`); a `weak`
  back-reference avoids the cycle.
- `free(obj)` runs the destructor now; with `defer` it closes resources on every exit.
- Memory you allocate in C is yours to free.

## Exercises

1. Give a class a destructor that prints, and predict the output of a program
   that stores objects in an `Array`, removes one, then clears the array.
2. Build a doubly linked list where each node has `next` and a `weak prev`.
   Check with destructors that dropping the head frees every node.
3. Create a cycle without `weak` and check that `GC.collect()` frees it.
4. Write a class that holds a callback which captures the object itself. Is
   that a cycle? How would you avoid it?
5. Run one of your programs with `--sanitize` and see whether it reports anything.

Next: [Modules and packages →](16-modules-and-packages.md)
