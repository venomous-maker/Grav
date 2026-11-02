# 6. Classes

## Defining a class

A class has fields, a constructor, and methods. `new` creates an instance (on the
heap) and instances are references: copying a variable copies the reference, not
the object. Inside methods, the instance is `self` (or its alias `this`).

```grav
class Counter {
    public count: int
    constructor(start: int) { self.count = start }
    fn increment() { self.count = self.count + 1 }
    fn get() -> int { return self.count }
}

fn main() {
    let a = new Counter(10)
    let b = a               // same object
    a.increment()
    b.increment()
    print(a.get())
}
```

Output:

```text
12
```

## Access control and readonly

| Modifier | Visible from |
|----------|-------------|
| `public` (default) | everywhere |
| `protected` | the class and its subclasses |
| `private` | the class itself |

`readonly` fields can only be assigned in a constructor. For every `private` field,
Grav generates public `get_<field>()` and (unless readonly) `set_<field>(v)`
accessors, unless the class defines a method with that name itself.

```grav
class User {
    readonly id: int
    private email: string
    constructor(id: int, email: string) { self.id = id; self.email = email }
}

fn main() {
    let u = new User(7, "ada@example.com")
    print(u.id)
    print(u.get_email())
    u.set_email("ada@lovelace.dev")
    print(u.get_email())
}
```

Output:

```text
7
ada@example.com
ada@lovelace.dev
```

Accessing a private field from outside (`u.email`) or assigning a readonly one
outside a constructor (`u.id = 8`) is a compile error.

## Static fields and methods

`static` members belong to the class rather than to instances. Access them through
the class name.

```grav
class IdGen {
    static next: int = 1
    static readonly PREFIX: string = "ID-"
    static fn make() -> string {
        let id = "${IdGen.PREFIX}${IdGen.next}"
        IdGen.next = IdGen.next + 1
        return id
    }
}

fn main() {
    print(IdGen.make())
    print(IdGen.make())
}
```

Output:

```text
ID-1
ID-2
```

## Constructors

A class may declare several constructors with different parameter types; `new` picks
the matching one. A class without a constructor gets an empty, parameterless one.

```grav
class Color {
    public r: int  public g: int  public b: int
    constructor() { self.r = 0; self.g = 0; self.b = 0 }
    constructor(gray: int) { self.r = gray; self.g = gray; self.b = gray }
    constructor(r: int, g: int, b: int) { self.r = r; self.g = g; self.b = b }
    fn toString() -> string { return "rgb(${self.r}, ${self.g}, ${self.b})" }
}

fn main() {
    print(new Color())
    print(new Color(128))
    print(new Color(255, 64, 0))
}
```

Output:

```text
rgb(0, 0, 0)
rgb(128, 128, 128)
rgb(255, 64, 0)
```

## toString()

A method `fn toString() -> string` makes objects printable: `print(obj)`, `str(obj)`,
and `"${obj}"` all call it.

## Destructors

`destructor() { ... }` runs when the object is released with `free(obj)`. See
[Memory](12-memory.md) for how destructors chain through a class hierarchy.

```grav
class Connection {
    private host: string
    constructor(host: string) { self.host = host; print("connect ${host}") }
    destructor() { print("disconnect ${self.host}") }
}

fn main() {
    let c = new Connection("db.local")
    print("working")
    free(c)
}
```

Output:

```text
connect db.local
working
disconnect db.local
```

## Method chaining

Returning `self` lets calls chain:

```grav
class Query {
    private parts: string
    constructor() { self.parts = "SELECT *" }
    fn from(t: string) -> Query { self.parts = self.parts + " FROM " + t; return self }
    fn where(c: string) -> Query { self.parts = self.parts + " WHERE " + c; return self }
    fn build() -> string { return self.parts }
}

fn main() {
    print(new Query().from("users").where("age > 30").build())
}
```

Output:

```text
SELECT * FROM users WHERE age > 30
```

## final

A `final class` cannot be extended and a `final fn` cannot be overridden. Besides
documenting intent, `final` guarantees calls to those methods are direct.

```grav
final class Version {
    public major: int
    constructor(major: int) { self.major = major }
    final fn label() -> string { return "v${self.major}" }
}

fn main() { print(new Version(2).label()) }
```

Output:

```text
v2
```

Next: [Inheritance & interfaces →](07-inheritance.md)
