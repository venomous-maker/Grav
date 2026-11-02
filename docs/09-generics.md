# 9. Generics

Generics let one definition work for many types. Grav generics are *monomorphized*:
each distinct set of type arguments compiles to its own specialized code, so there
is no boxing or runtime cost.

## Generic classes

```grav
class Box<T> {
    private value: T
    constructor(v: T) { self.value = v }
    fn get() -> T { return self.value }
    fn set(v: T) { self.value = v }
}

fn main() {
    let n = new Box<int>(5)
    n.set(n.get() * 2)
    let s = new Box<string>("hi")
    print("${n.get()} ${s.get()}")
}
```

Output:

```text
10 hi
```

## Generic structs, interfaces, and inheritance

```grav
struct Pair<A, B> { first: A  second: B }

interface Source<T> { fn next() -> T }

class Counter implements Source<int> {
    private n: int
    constructor() { self.n = 0 }
    fn next() -> int { self.n = self.n + 1; return self.n }
}

class Stack<T> {
    protected items: T[8]
    protected size: int
    constructor() { self.size = 0 }
    fn push(x: T) { self.items[self.size] = x; self.size = self.size + 1 }
    fn pop() -> T { self.size = self.size - 1; return self.items[self.size] }
}

class LoggedStack<T> extends Stack<T> {        // generic inheritance
    constructor() { super() }
    fn push(x: T) { print("push ${x}"); self.items[self.size] = x; self.size = self.size + 1 }
}

fn main() {
    let p = Pair<string, int> { first: "age", second: 36 }
    print("${p.first}=${p.second}")
    let src: Source<int> = new Counter()
    src.next()
    print(src.next())
    let st = new LoggedStack<string>()
    st.push("a")
    st.push("b")
    print(st.pop())
}
```

Output:

```text
age=36
2
push a
push b
b
```

## Generic functions

Pass type arguments with `::<...>` (they are not inferred from the arguments).

```grav
fn firstOf<T>(a: T, b: T, pickFirst: bool) -> T { return pickFirst ? a : b }
fn describe<K, V>(k: K, v: V) -> string { return "${k} -> ${v}" }

fn main() {
    print(firstOf::<string>("x", "y", false))
    print(describe::<string, float>("pi", 3.14))
}
```

Output:

```text
y
pi -> 3.14
```

## Constraints

`T: Bound` requires the type argument to be (a subclass of) a class or to implement an
interface, which lets the body call the bound's methods.

```grav
interface Named { fn name() -> string }

class City implements Named {
    private n: string
    constructor(n: string) { self.n = n }
    fn name() -> string { return self.n }
}

fn shout<T: Named>(x: T) -> string { return x.name() + "!" }

fn main() { print(shout::<City>(new City("Nairobi"))) }
```

Output:

```text
Nairobi!
```

## Generic methods

A method can have type parameters of its own — on generic or plain classes, instance
or static.

```grav
class Cell<T> {
    private v: T
    constructor(v: T) { self.v = v }
    fn get() -> T { return self.v }
    fn map<U>(f: fn(T) -> U) -> Cell<U> { return new Cell<U>(f(self.v)) }
}

class Util {
    static fn pair<A, B>(a: A, b: B) -> string { return "(${a}, ${b})" }
}

fn main() {
    let c = new Cell<int>(21)
    let label = c.map::<string>(fn(n) => "#${n * 2}")
    print(label.get())
    print(Util.pair::<int, bool>(1, true))
}
```

Output:

```text
#42
(1, true)
```

## Generic aliases

```grav
type Triple<T> = T[3]

fn main() {
    let t: Triple<int> = [1, 2, 3]
    print(t[0] + t[1] + t[2])
}
```

Output:

```text
6
```

The standard collections (`Array<T>`, `List<T>`, `Map<K, V>`, `HashMap<K, V>`, …) are
generic classes written in Grav — see [Standard library](14-standard-library.md).

Next: [Lambdas & closures →](10-lambdas.md)
