# 11. Inheritance and interfaces

Often several classes are variations of one idea: dogs and cats are animals;
circles and squares are shapes; files and sockets are things you can read from.
This chapter covers the two ways Grav expresses "these types share something":

- **Inheritance**: a class *extends* another and gets its fields and methods,
  adding or changing what it needs. "A dog *is an* animal."
- **Interfaces**: a class promises to provide certain methods, and code can
  work with anything that keeps the promise. "A file *can be* read from."

In this chapter you will learn:

- subclasses, overriding methods, and `super`,
- abstract classes, which exist only to be extended,
- interfaces and traits,
- checking and converting types at run time with `is`, `as` and `typename`,
- composition with `uses`, and multiple inheritance.

## Subclasses

`class Dog extends Animal` makes `Dog` a *subclass* of `Animal`. A dog has
everything an animal has, and can add more or change behaviour by *overriding*
methods:

```grav
class Animal {
    protected name: string
    constructor(name: string) { self.name = name }
    fn speak() -> string { return "${self.name} makes a sound" }
    fn introduce() -> string { return "I am ${self.name}. ${self.speak()}" }
}

class Dog extends Animal {
    constructor(name: string) { super(name) }
    fn speak() -> string { return "${self.name} barks" }
}

class Puppy extends Dog {
    fn speak() -> string { return super.speak() + " (softly)" }
}

fn main() {
    let animals = [new Animal("Generic"), new Dog("Rex"), new Puppy("Bit")]
    for (a in animals) {
        print(a.introduce())
    }
}
```

Output:

```text
I am Generic. Generic makes a sound
I am Rex. Rex barks
I am Bit. Bit barks (softly)
```

Several things are going on:

- **A subclass object can be used wherever its parent class is expected.** The
  array holds `Animal`s, and dogs and puppies are animals.
- **Methods are virtual.** `introduce` is written once, in `Animal`, but when it
  calls `self.speak()` it runs the *object's* version: a dog barks even though the
  call is made from `Animal`'s code. This is *polymorphism*: one call, different
  behaviour depending on the actual object.
- **`super(name)`** in a constructor runs the parent's constructor, so the
  parent can set up its own fields.
- **`super.speak()`** calls the parent's version of a method, so an override
  can extend it instead of replacing it. `super` reaches the rest of the
  parent too: its fields (`super.id = 0`, `super.count += 1`, read like
  `self.id`), its static fields and static methods (`super.make()` is
  `Animal.make()`), and a method as a value (`let f = super.speak`, bound to
  this object). It works inside lambdas as well.
- **`protected`** fields are visible to subclasses but not to outside code.

Rules worth knowing:

- An override must have the same parameter and result types as the method it
  overrides (a method returning `Self` is overridden by one returning `Self`).
- A subclass that declares no constructor inherits its parent's, which is why
  `Puppy` needs none: `new Puppy("Bit")` uses `Dog`'s.
- A constructor that does not call `super(...)` first runs the parent's
  constructor without arguments. If the parent has none, the compiler asks you to
  call `super(...)`.

## Self: methods and statics that fit every subclass

A method that returns `self` is usually declared to return its class, so in a
subclass the chain falls back to the parent type and the subclass's own methods
are lost. Declaring the result as `Self` keeps the caller's class: each call
returns the type it was called on. Static methods can name `Self` too, for the
class they are called through, so one `find` in a base class makes users when
called as `User.find(...)`:

```grav
import "std"

class Query {
    protected parts: Array<string>
    constructor() { self.parts = new Array<string>() }
    fn where(cond: string) -> Self { self.parts.push(cond); return self }
    fn build() -> string { return "WHERE " + Str.join(self.parts, " AND ") }
}

class UserQuery extends Query {
    fn active() -> Self { return self.where("active = 1") }
}

class Model {
    public id: int
    constructor() { self.id = 0 }
    static fn table() -> string { return "models" }
    static fn find(id: int) -> Self {
        let m = new Self()
        m.id = id
        print("SELECT * FROM " + Self.table() + " WHERE id = ${id}")
        return m
    }
}

class User extends Model {
    static fn table() -> string { return "users" }
    fn greet() -> string { return "hi, user ${self.id}" }
}

fn main() {
    print(new UserQuery().where("age > 18").active().build())
    print(User.find(7).greet())
}
```

Output:

```text
WHERE age > 18 AND active = 1
SELECT * FROM users WHERE id = 7
hi, user 7
```

- **In an instance method**, `-> Self` as the result type means the receiver's
  own class, and the method returns `self` (or `null`, or what another `Self`
  method returns for `self`), so the caller's type is always right. An override
  of it returns `Self` too. In the body, `Self.count` and `Self.make()` reach the
  static members of the class that declares the method, as its name would; in a
  trait's method that is each class that implements the trait, so each has its
  own count.
- **In a static method**, `Self` is the class the call names: `new Self(...)`,
  `Self.table()` and types like `Array<Self>` all mean `User` in `User.find`. The
  compiler makes a copy of the method for each class it is called through.
  `Self.table()` therefore reaches `User`'s own `table`, and `new Self()` needs a
  constructor that every such class has.
- `Self` is not a parameter type in a class, and an instance method cannot write
  `new Self()` or `x is Self` (in a subclass's object they would still mean the
  declaring class; return `self`, or call a static method, instead). A method returning `Self` cannot yet be called through an interface
  value.

## Abstract classes

Sometimes the parent class is only an idea: there is no such thing as a plain
"shape", only circles and squares. Mark such a class `abstract`. It cannot be
created with `new`, and its `abstract` methods have no body: every subclass must
provide them.

```grav
abstract class Shape {
    abstract fn area() -> float
    abstract fn name() -> string
    fn describe() -> string { return "${self.name()} with area ${self.area()}" }
}

class Square extends Shape {
    private side: float
    constructor(side: float) { self.side = side }
    fn area() -> float { return self.side * self.side }
    fn name() -> string { return "square" }
}

class Circle extends Shape {
    private r: float
    constructor(r: float) { self.r = r }
    fn area() -> float { return 3.14159 * self.r * self.r }
    fn name() -> string { return "circle" }
}

fn main() {
    let shapes: Shape[2] = [new Square(3.0), new Circle(1.0)]
    for (s in shapes) { print(s.describe()) }
}
```

Output:

```text
square with area 9
circle with area 3.14159
```

Forgetting to implement an abstract method in a subclass is a compile error, and
so is `new Shape()`.

## Interfaces

Inheritance says what a class *is*. An *interface* says what a class *can do*. It
lists method signatures without bodies; a class `implements` it by providing those
methods. A class has one parent, but can implement any number of interfaces:

```grav
interface Drawable {
    fn draw() -> string
}

interface Resizable {
    fn resize(factor: int)
}

class Box implements Drawable, Resizable {
    private size: int
    constructor(size: int) { self.size = size }
    fn draw() -> string { return "[box ${self.size}]" }
    fn resize(factor: int) { self.size *= factor }
}

class Label implements Drawable {
    private text: string
    constructor(text: string) { self.text = text }
    fn draw() -> string { return "<${self.text}>" }
}

fn render(d: Drawable) -> string {
    return "rendering " + d.draw()
}

fn main() {
    let b = new Box(2)
    let r: Resizable = b
    r.resize(5)
    print(render(b))
    print(render(new Label("hi")))
}
```

Output:

```text
rendering [box 10]
rendering <hi>
```

`render` does not care what it is given, only that it can be drawn. That makes it
work with classes written long after `render` was, which is what interfaces are for:
code that depends on capabilities rather than on specific classes.

A class that says it implements an interface but misses a method is a compile
error that names the method.

### Traits: interfaces with default methods

A `trait` is an interface whose methods may have bodies. Classes that implement
it get those methods for free and only need to write the rest. Inside a trait,
`Self` means "whatever class implements this":

```grav
trait Comparable {
    fn compareTo(other: Self) -> int
    fn isLessThan(other: Self) -> bool { return self.compareTo(other) < 0 }
    fn isEqualTo(other: Self) -> bool { return self.compareTo(other) == 0 }
}

class Version implements Comparable {
    public n: int
    constructor(n: int) { self.n = n }
    fn compareTo(other: Version) -> int { return self.n - other.n }
}

fn main() {
    let a = new Version(3)
    let b = new Version(5)
    print(a.isLessThan(b))
    print(a.isEqualTo(b))
}
```

Output:

```text
true
false
```

### Traits with state

A trait can also bring fields, static fields, private helpers and decorators.
A class that implements it gets them as if it had written them itself, so one
trait can add the same piece of state and behaviour to many unrelated classes:

```grav
import "std"

@Audited
trait Versioned {
    public version: int = 1
    private history: Array<string> = new Array<string>()
    

    fn change(what: string) -> Self {
        self.history.push("v${self.version}: ${what}")
        self.version += 1
        return self
    }
    fn log() -> string { return self.joined(", ") }
    private fn joined(sep: string) -> string {
        let out = ""
        for (i, h in self.history) { out += (i > 0 ? sep : "") + h }
        return out
    }
}

class Page implements Versioned {
    public title: string
    constructor(title: string) { self.title = title }
}

fn main() {
    let p = new Page("Home")
    p.change("draft").change("review")
    print(p.version)
    print(p.log())
    print(ClassInfo.of(p).has("Audited"))
    let v: Versioned = p
    print(v.log())
}
```

Output:

```text
3
v1: draft, v2: review
true
v1: draft, v2: review
```

`Page` now has a `version` field (starting at 1), a private `history`, and the
`@Audited` decorator, which reflection sees. The class's own members win: had
`Page` declared `version` itself, the trait's would be left out. A subclass of
`Page` inherits these members from `Page`, so it does not get a second copy. A
trait has no constructor, so give its fields initial values instead.

## Checking types at run time

A variable of type `Animal` may hold a `Dog` or a `Cat`. Usually virtual methods
mean you do not need to know which. When you do:

| Expression | Gives |
|------------|-------|
| `x is Cat` | whether the object is a `Cat` (or a subclass of it), or implements an interface `Cat` |
| `x as Cat` | the same object, typed as a `Cat`; throws `TypeError` if it is not one |
| `typename(x)` | the name of the object's actual class |
| `isInstance(x, Cat)` | the same as `x is Cat` |

```grav
class Animal { }
class Cat extends Animal {
    fn purr() -> string { return "purr" }
}

fn main() {
    let a: Animal = new Cat()
    print(typename(a))
    print(a is Cat)
    if (a is Cat) {
        let c = a as Cat
        print(c.purr())
    }
}
```

Output:

```text
Cat
true
purr
```

`is` and `as` work between interfaces too: given a `Shape`, you can ask whether it
is also `Named`:

```grav
import "std"

interface Shape { fn area() -> float }
interface Named { fn name() -> string }

class Square implements Shape, Named {
    fn area() -> float { return 4.0 }
    fn name() -> string { return "square" }
}
class Blob implements Shape {
    fn area() -> float { return 3.0 }
}

fn main() {
    let shapes: Shape[2] = [new Square(), new Blob()]
    for (s in shapes) {
        if (s is Named) { print("${(s as Named).name()}: ${s.area()}") }
        else { print("unnamed: ${s.area()}") }
    }
}
```

Output:

```text
square: 4
unnamed: 3
```

If you find yourself writing long chains of `if (x is A) ... else if (x is B)`,
consider a virtual method instead (each class answers for itself), or a
[data enum](08-structs-enums-tuples.md#data-enums-and-pattern-matching), whose
`match` checks that every case is handled.

## Composition with uses

Inheritance is not the only way to reuse code. Often it is better for an object
to *have* a helper than to *be* one. `uses field: Type` adds a private field and
makes the helper's public methods available on the class, forwarding to it:

```grav
class Logger {
    fn log(msg: string) -> string { return "[log] " + msg }
}

class Service uses logger: Logger {
    constructor() { self.logger = new Logger() }
    fn start() -> string { return self.log("started") }
}

fn main() {
    let s = new Service()
    print(s.start())
    print(s.log("called directly"))
}
```

Output:

```text
[log] started
[log] called directly
```

A `Service` is not a `Logger` (it cannot be passed where a `Logger` is
expected), it just has the same methods. A good rule of thumb: inherit when the
"is a" sentence is true, compose otherwise.

## Multiple inheritance

`class C extends A, B` copies the fields and methods of every parent into `C`:

```grav
class Walker {
    public steps: int
    constructor() { self.steps = 0 }
    fn walk() -> string { self.steps += 1; return "walk" }
}
class Swimmer {
    public laps: int
    constructor() { self.laps = 0 }
    fn swim() -> string { self.laps += 1; return "swim" }
}
class Frog extends Walker, Swimmer {
    constructor() { self.steps = 0; self.laps = 0 }
}

fn main() {
    let f = new Frog()
    f.walk()
    f.swim()
    f.swim()
    print("${f.steps} steps, ${f.laps} laps")
}
```

Output:

```text
1 steps, 2 laps
```

Only the first parent is a true parent: a `Frog` can be used as a `Walker`, but
not passed where a `Swimmer` is expected. When you need that, make `Swimmer` an
interface. Most designs are clearer with one parent plus interfaces, so reach for
multiple inheritance rarely.

## Summary

- `class B extends A` inherits `A`'s members; a `B` can be used as an `A`.
- Methods are virtual: the object's own override runs. `super(...)`,
  `super.method()`, `super.field` and the parent's statics reach the parent.
- `-> Self` returns the receiver as the caller's own class; in a static method,
  `Self` is the class the call names (`User.find(1)` makes a `User`).
- `abstract` classes cannot be created; their `abstract` methods must be implemented.
- An `interface` lists methods; a class can implement many. A `trait` can provide
  default method bodies, and fields, helpers and decorators its classes take in.
- `x is T`, `x as T` and `typename(x)` inspect types at run time.
- `uses` forwards to a helper object; `extends A, B` flattens several classes.

## Exercises

1. Build an `Employee` base class with `pay() -> float`, and subclasses
   `Salaried` and `Hourly` that compute pay differently. Print the total payroll
   of a mixed array.
2. Make `Employee` abstract. What error do you get with `new Employee(...)`?
3. Define an interface `Plugin { fn name() -> string; fn run(input: string) -> string }`
   and two plugins. Run every plugin in an `Array<Plugin>` on the same input.
4. Write a trait `Describable` with `fn title() -> string` and a default
   `fn banner() -> string` that puts the title between `===` lines.
5. Given an `Array<Animal>`, count how many are `Cat`s using `is`.

Next: [Operators and overloading →](12-operators-and-overloading.md)
