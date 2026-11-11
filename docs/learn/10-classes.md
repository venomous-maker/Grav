# 10. Classes

A *class* describes a kind of object: what data it holds (its *fields*) and
what it can do (its *methods*). A bank account has a balance and can take
deposits; a game character has a position and can move. Classes let you bundle
data with the code that looks after it, so the rest of the program can use an
account without knowing how its balance is stored.

In this chapter you will learn:

- how to define a class with fields, a constructor and methods,
- how objects differ from structs: they are shared, not copied,
- how to hide fields with `private` and protect them with `readonly`,
- static members, which belong to the class instead of each object,
- multiple constructors, `toString`, destructors and `final`.

## Defining a class

```grav
class Account {
    public owner: string
    public balance: int

    constructor(owner: string) {
        self.owner = owner
        self.balance = 0
    }

    fn deposit(amount: int) {
        self.balance += amount
    }

    fn describe() -> string {
        return "${self.owner} has ${self.balance}"
    }
}

fn main() {
    let acct = new Account("Ada")
    acct.deposit(50)
    acct.deposit(25)
    print(acct.describe())
    print(acct.balance)
}
```

Output:

```text
Ada has 75
75
```

- **Fields** (`owner`, `balance`) are declared with a name and a type.
- **The constructor** runs when an object is created with `new Account(...)`.
  Its job is to give the fields their starting values.
- **Methods** are functions that belong to the class. They are called on an
  object: `acct.deposit(50)`.
- **`self`** is the object the method was called on (`this` is a synonym). Fields
  are always reached through it: `self.balance`.

Each `new` creates a separate object with its own fields. Fields the
constructor does not set start as zero values: `0`, `false`, `""` or `null`.

## Objects are shared

Here is the big difference from structs. A variable of a class type holds a
*reference* to an object, not the object itself. Assigning it, or passing it to
a function, gives another reference to **the same** object:

```grav
class Counter {
    public count: int
    constructor() { self.count = 0 }
    fn increment() { self.count += 1 }
}

fn bump(c: Counter) {
    c.increment()
}

fn main() {
    let a = new Counter()
    let b = a              // b and a are the same counter
    a.increment()
    b.increment()
    bump(a)
    print(a.count)
    print(a == b)          // the same object?
}
```

Output:

```text
3
true
```

This is what you want for things that have an identity: there is one account,
one connection, one game world, and everyone who uses it should see the same
one. For plain data that should be copied, use a [struct](08-structs-enums-tuples.md#structs).

`==` on objects asks "is this the same object?" unless the class defines its own
`operator==` ([chapter 12](12-operators-and-overloading.md)). An object
variable can also be `null`, meaning it refers to no object
([Null safety](09-null-safety.md)).

## Hiding fields: access control

So far anyone could write `acct.balance = -1000000`. To protect an object's
data, make fields `private` and let methods decide what is allowed:

| Modifier | Who can use the member |
|----------|------------------------|
| `public` (the default) | everyone |
| `protected` | the class and its subclasses ([chapter 11](11-inheritance-and-interfaces.md)) |
| `private` | only the class's own methods |

```grav
import "std"

class Account {
    private balance: int
    readonly owner: string

    constructor(owner: string) {
        self.owner = owner
        self.balance = 0
    }

    fn deposit(amount: int) {
        if (amount <= 0) { throw new ValueError("deposit must be positive") }
        self.balance += amount
    }

    fn withdraw(amount: int) -> bool {
        if (amount > self.balance) { return false }
        self.balance -= amount
        return true
    }
}

fn main() {
    let acct = new Account("Ada")
    acct.deposit(100)
    print(acct.withdraw(30))
    print(acct.withdraw(500))
    print(acct.get_balance())
    print(acct.owner)
}
```

Output:

```text
true
false
70
Ada
```

- `acct.balance` from outside the class is now a compile error
  (`field 'balance' is private in 'Account'`).
- `readonly` fields can be read by anyone but only assigned in a constructor.
- For every `private` field, Grav generates a public `get_<field>()` method, and a
  `set_<field>(value)` method unless the field is `readonly`. If you define a
  method with that name yourself, yours is used instead. Define your own when you
  want to check values or keep a field write-protected.

Hiding fields means you can later change how an object stores its data (say,
keep the balance in cents) without changing any code that uses it.

## Static members

A `static` field or method belongs to the class itself, not to any one object.
You use it through the class name:

```grav
class IdGen {
    static next: int = 1
    static readonly PREFIX: string = "ID-"

    static fn make() -> string {
        let id = "${IdGen.PREFIX}${IdGen.next}"
        IdGen.next += 1
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

A static field can hold any value, objects and collections included; like a
global's, its initializer runs once at start-up when it is not a plain literal.
Methods can be called on it directly (`Cache.items.size()`), and a lock or
counter kept there is shared by every thread:

```grav
import "std"

class Cache {
    static items: HashMap<string, int> = {"a": 1}
    static lock: Mutex = new Mutex()
}

fn main() {
    Cache.lock.lock()
    Cache.items.put("b", 2)
    Cache.lock.unlock()
    print(Cache.items.size())
}
```

Output:

```text
2
```

Static methods have no `self`. They are useful for helpers related to a class,
and for *factory* methods that create objects in a particular way, like
`Color.gray(128)`. Much of the standard library is static functions grouped in a
class: `Math.sqrt`, `Str.trim`, `File.exists`.

## Constructors

A class can have several constructors, as long as their parameters differ.
`new` picks the one that fits the arguments. Constructors can also have default
values and be called with named arguments, like functions:

```grav
class Color {
    public r: int
    public g: int
    public b: int
    constructor() { self.r = 0; self.g = 0; self.b = 0 }
    constructor(gray: int) { self.r = gray; self.g = gray; self.b = gray }
    constructor(r: int, g: int, b: int) { self.r = r; self.g = g; self.b = b }
    fn toString() -> string { return "rgb(${self.r}, ${self.g}, ${self.b})" }
}

class Window {
    public w: int
    public h: int
    constructor(w: int = 640, h: int = 480) { self.w = w; self.h = h }
}

fn main() {
    print(new Color())
    print(new Color(128))
    print(new Color(255, 64, 0))
    let win = new Window(h: 600)
    print("${win.w}x${win.h}")
}
```

Output:

```text
rgb(0, 0, 0)
rgb(128, 128, 128)
rgb(255, 64, 0)
640x600
```

A class with no constructor gets an empty one that takes no arguments.

## Making objects printable: toString

A method `fn toString() -> string` decides how an object looks when printed. It is
used by `print(obj)`, `str(obj)` and `"${obj}"`. Without one, an object prints as
its class name. `Color` above uses it.

## Clean-up: destructors

A `destructor() { ... }` runs when an object is no longer used. Grav counts
the references to every object, and the moment the last one goes away (the
variable ends, is reassigned, or the object is removed from a collection), the
object is destroyed:

```grav
class Connection {
    private host: string
    constructor(host: string) {
        self.host = host
        print("connect ${host}")
    }
    destructor() { print("disconnect ${self.host}") }
}

fn work() {
    let c = new Connection("db.local")
    print("working with ${c != null}")
}

fn main() {
    work()
    print("back in main")
    let d = new Connection("first")
    d = new Connection("second")      // "first" has no more references
    print("end of main")
}
```

Output:

```text
connect db.local
working with true
disconnect db.local
back in main
connect first
connect second
disconnect first
end of main
disconnect second
```

Because destruction happens at a predictable moment, a destructor is a good place
to close files, sockets and other resources. [Memory](15-memory.md) explains
reference counting in detail, including what happens with objects that refer to
each other.

## Method chaining

A method that returns `self` lets calls be chained, which reads nicely for
builders and configuration:

```grav
class Query {
    private text: string
    constructor() { self.text = "SELECT *" }
    fn from(table: string) -> Query { self.text += " FROM " + table; return self }
    fn where(cond: string) -> Query { self.text += " WHERE " + cond; return self }
    fn build() -> string { return self.text }
}

fn main() {
    print(new Query().from("users").where("age > 30").build())
}
```

Output:

```text
SELECT * FROM users WHERE age > 30
```

Declaring the result as `Self` instead of `Query` keeps such chains working in
subclasses (see [Self](11-inheritance-and-interfaces.md#self-methods-and-statics-that-fit-every-subclass)).

## final

A `final class` cannot be extended, and a `final fn` cannot be overridden by a
subclass (inheritance is the next chapter). Use it to say "this is not designed
to be changed by subclasses". As a bonus, calls to final methods are direct calls,
which are slightly faster.

```grav
final class Version {
    public major: int
    constructor(major: int) { self.major = major }
    final fn label() -> string { return "v${self.major}" }
}

fn main() {
    print(new Version(2).label())
}
```

Output:

```text
v2
```

## Structs or classes?

| | struct | class |
|-|--------|-------|
| assigning / passing | copies | shares the same object |
| can be `null` | no (unless `T?`) | yes |
| inheritance, interfaces | no | yes |
| destructor | no | yes |
| cost | stored inline, no allocation | allocated, reference counted |
| good for | small values: points, colours, money | things with identity: accounts, connections, windows |

## Summary

- `class Name { fields  constructor(...) { }  methods }`, created with `new Name(...)`.
- Object variables are references: assigning or passing shares the object.
- `private` hides a member, `protected` shares it with subclasses, `readonly`
  fields are set only in constructors; private fields get `get_`/`set_` methods.
- `static` members belong to the class.
- Several constructors are allowed, with defaults and named arguments.
- `toString` controls printing; a `destructor` runs when the last reference goes away.

## Exercises

1. Write a `Stopwatch` class with `start()`, `stop()` and `elapsedMs()` methods
   (use `Time.nowMs()` from the standard library).
2. Write a `Temperature` class that stores Celsius privately and has `celsius()`,
   `fahrenheit()` and a static factory `fromFahrenheit(f: float)`.
3. Add a static counter to a class that counts how many objects have been
   created. Print it after creating three.
4. Give `Account` a `transfer(to: Account, amount: int) -> bool` method. Why does
   it change the other account even though it is "just a parameter"?
5. Add a destructor to a class, put three objects in an `Array`, then call
   `clear()` on the array. When do the destructors run?

Next: [Inheritance and interfaces →](11-inheritance-and-interfaces.md)
