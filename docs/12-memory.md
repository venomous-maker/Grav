# 12. Memory

Grav has no garbage collector and no runtime overhead for memory management. Objects
are released explicitly, cleanup is made reliable with destructors and `defer`, and
the compiler frees strings and closures automatically wherever it can prove it is
safe.

## new and free

`new` allocates an object; `free(obj)` releases it. Freeing an object runs its
destructor chain first — the dynamic type's destructor, then each base class's.

```grav
class Resource {
    protected name: string
    constructor(name: string) { self.name = name }
    destructor() { print("release ${self.name}") }
}
class Database extends Resource {
    constructor(name: string) { super(name) }
    destructor() { print("flush ${self.name}") }     // runs first
}

fn main() {
    let r: Resource = new Database("main")
    free(r)
}
```

Output:

```text
flush main
release main
```

The standard collections have destructors that free their internal buffers, so
`free(list)` releases everything the list allocated (but not objects stored in it —
free those yourself if nothing else refers to them).

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

## Automatic string freeing

Strings produced by `+`, interpolation, `str()`, and string functions are heap
allocated. The compiler analyses each function and frees a local string variable
automatically when it can prove the variable *owns* its value — that every use only
reads or copies the text (part of another `+` chain, printed, compared, a `switch`
subject, or passed to a library call that keeps no reference). An owned variable
frees its old value when reassigned (including `+=`) and its final value when its
block exits.

```grav
import "../lib/std.grav"

fn main() {
    let report = ""
    for (i in 0..1000) {
        let line = "item ${i}"           // freed at the end of every iteration
        if (i % 250 == 0) { report += line + "; " }   // old `report` freed each time
    }
    IO.print("report: " + report)        // this temporary is freed after the call
}
```

Output:

```text
report: item 0; item 250; item 500; item 750; 
```

Ownership crosses function boundaries: a function whose every `return` hands back a
new string gives it to the caller, who frees it in turn.

```grav
fn banner(title: string) -> string { return "== " + title + " ==" }

fn main() {
    let b = banner("Grav")               // owned by main, freed at its end
    print(b)
    banner("unused")                     // a discarded result is freed at once
}
```

Output:

```text
== Grav ==
```

A string is left alone (never freed automatically) when it may be shared: when it is
returned from a function that sometimes returns a borrowed string, stored in a field,
array, or collection, captured by a lambda, passed to your own functions, or named in
inline C. Literals are never freed.

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

A lambda's captured variables live in a small heap environment. It is freed
automatically for a local function value that is only ever called, and for a
capturing lambda passed directly to `forEach`/`map`/`filter`/`reduce`/`sortBy`/…;
`free(f)` releases one explicitly.

## Pointers and C memory

Pointers obtained from inline C (for example a `malloc`ed buffer) are released with
`free(p)` as in C. The `Array<T>` implementation in `lib/array.grav` is a good
example of managing raw memory from Grav.

Next: [Modules, namespaces & C interop →](13-modules.md)
