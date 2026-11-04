# 7. Inheritance & interfaces

## Subclasses and virtual methods

`class B extends A` inherits A's fields and methods. Methods are virtual: calling a
method through an `A` reference runs the most-derived override.

```grav
class Animal {
    protected name: string
    constructor(name: string) { self.name = name }
    fn speak() -> string { return "${self.name} makes a sound" }
}

class Dog extends Animal {
    constructor(name: string) { super(name) }          // run the parent constructor
    fn speak() -> string { return "${self.name} barks" }
}

class Puppy extends Dog {
    constructor(name: string) { super(name) }
    fn speak() -> string { return super.speak() + " (softly)" }   // reuse the parent's
}

fn main() {
    let animals = [new Animal("Generic") as Animal, new Dog("Rex") as Animal, new Puppy("Bit") as Animal]
    for (a in animals) { print(a.speak()) }
}
```

Output:

```text
Generic makes a sound
Rex barks
Bit barks (softly)
```

An override must keep the parent method's parameter and return types.

A subclass without a constructor of its own inherits its parent's constructors, so
`Puppy` above could leave its constructor out and still be built with
`new Puppy("Bit")`. A constructor that never calls `super(...)` runs the parent's
parameterless constructor first; if the parent has no such constructor, leaving out
`super(...)` is a compile error.

## Abstract classes

An `abstract class` cannot be instantiated; its `abstract fn` methods have no body
and every concrete subclass must implement them.

```grav
abstract class Shape {
    abstract fn area() -> float
    fn describe() -> string { return "area ${self.area()}" }
}

class Square extends Shape {
    private side: float
    constructor(side: float) { self.side = side }
    fn area() -> float { return self.side * self.side }
}

fn main() {
    let s: Shape = new Square(3.0)
    print(s.describe())
}
```

Output:

```text
area 9
```

## Interfaces

An interface lists method signatures. A class `implements` any number of them, and a
value of the interface type can hold any implementing object.

```grav
interface Drawable { fn draw() -> string }
interface Resizable { fn resize(factor: int) }

class Box implements Drawable, Resizable {
    private size: int
    constructor(size: int) { self.size = size }
    fn draw() -> string { return "[box ${self.size}]" }
    fn resize(factor: int) { self.size = self.size * factor }
}

fn render(d: Drawable) -> string { return "rendering " + d.draw() }

fn main() {
    let b = new Box(2)
    let r: Resizable = b
    r.resize(5)
    print(render(b))
}
```

Output:

```text
rendering [box 10]
```

## Traits

A `trait` is an interface whose methods may have default bodies. Inside a trait,
`Self` means "the implementing type".

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

## Composition with `uses`

`uses field: Type` adds a private field and forwards `Type`'s public methods to it, so
the class gains behaviour without an is-a relationship.

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

## Multiple inheritance

`class C extends A, B` flattens the fields and methods of every base into C. The
first base keeps full polymorphism (a `C` can be used as an `A`); use interfaces when
you need polymorphism through the others.

```grav
class Walker {
    public steps: int
    constructor() { self.steps = 0 }
    fn walk() -> string { self.steps = self.steps + 1; return "walk" }
}
class Swimmer {
    public laps: int
    constructor() { self.laps = 0 }
    fn swim() -> string { self.laps = self.laps + 1; return "swim" }
}
class Frog extends Walker, Swimmer {
    constructor() { self.steps = 0; self.laps = 0 }
}

fn main() {
    let f = new Frog()
    f.walk()
    f.swim()
    f.swim()
    print("${f.steps} steps, ${f.laps} laps, swimmer: ${f is Swimmer}")
}
```

Output:

```text
1 steps, 2 laps, swimmer: true
```

## Runtime type information

| Expression | Result |
|------------|--------|
| `typename(obj)` | the dynamic class name |
| `isInstance(obj, T)` / `obj is T` | is the object a `T` or a subclass (or, for an interface `T`, does it implement it)? |
| `obj as T` | a cast within the hierarchy or to an interface (one the object does not support throws `TypeError`) |

```grav
class Animal { }
class Cat extends Animal { fn purr() -> string { return "purr" } }

fn main() {
    let a: Animal = new Cat()
    print(typename(a))
    print(a is Cat)
    print(isInstance(a, Animal))
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
true
purr
```

Next: [Overloading & operators →](08-overloading.md)
