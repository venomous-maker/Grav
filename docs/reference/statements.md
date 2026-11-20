# Declarations and statements

A summary of every form, with the rules that apply. `[...]` marks optional parts.

## Top-level declarations

```text
fn name<T: Bound>(a: A, b: B = default, ...rest: R) -> Ret { ... }
async fn name(a: A) -> T { ... }
[abstract | final] class Name<T> [extends Base<T>, Other] [implements I, J] [uses field: Type] { members }
struct Name<T> { field: T  other: int  fn method() -> T { ... } }
enum Name { A, B = 5, C }
enum Name<T> { Variant(field: T), Other(int, string), Empty,  fn method() { ... } }
interface Name<T> { fn method(x: T) -> R }
trait Name { fn required(o: Self) -> int  fn provided() -> bool { ... }  field: T = init  ... }
type Alias<T> = Type          (typedef is a synonym)
let name: T = value           (a global variable; the type is required; a non-literal value is
                              computed at start-up, before main, in declaration order)
const NAME: T = value         (a global constant)
namespace a.b { declarations }
import "path" | import "path" as alias | import { A, B as C } from "path"
use ns::* | use ns.Name [as Alias] | use ns as alias
test "name" { statements }
#define NAME text | #define F(x) text
%{ C declarations %}
@Decorator | @Decorator(args) | export      (before a declaration; documentation only)
```

### Functions

- Parameter types are required; the result type defaults to `void`.
- Parameters with defaults must come after those without. Defaults are evaluated
  per call, in the declaring file's scope.
- One variadic parameter, `...name: T`, may come last (not in `async` functions).
- Functions may be overloaded by parameter types.
- A non-`void` function must return on every path.

### Class members

```text
[public | private | protected] [static] [readonly] [weak] name: Type [= init]   (init only for static fields)
constructor(params) [{ super(args) ... }]     (may be overloaded)
destructor() { ... }
[public | private | protected] [static | abstract | final] fn name<U>(params) -> T { ... }
fn operator+(o: T) -> R { ... }               (also - * / % == != < <= > >= [] []=)
```

- Members are `public` unless marked otherwise.
- Every `private` field gets generated `get_<field>()` and (unless `readonly`)
  `set_<field>(v)` methods, unless the class defines one with that name.
- A class without a constructor gets a parameterless one, or inherits its parent's.
- A constructor that does not call `super(...)` calls the parent's parameterless
  constructor first.
- Overrides must keep the parameter and result types.
- `abstract` methods have no body and make the class abstract.

### Traits

A trait is an interface that may also bring code and state. Its members:

```text
fn name(params) -> T                          (required: each class writes it)
fn name(params) -> T { ... }                  (a default: the class may write its own)
[public | private | protected] [readonly] [weak] name: Type [= init]
[public | private | protected] static name: Type = init
[private | protected] fn name(params) -> T { ... }
[public | private | protected] static fn name(params) -> T { ... }
@Decorator trait Name { ... }
```

- A class that `implements` the trait gets its fields, static fields, private,
  protected and static methods and decorators as if it had written them; its
  public methods are the trait's interface, so `x is Name` and `let n: Name = x`
  work.
- What the class writes itself wins: its own field, method or decorator of the
  same name is kept, and the trait's is left out.
- A subclass of a class that implements the trait inherits these members; it does
  not get a second copy, even if it names the trait again.
- Each class gets its own copy of a static field.
- Names in a trait's members mean what they mean where the trait is declared,
  whichever namespace the class is in.
- A trait has no constructor (give its fields initializers instead), and a
  generic trait (`trait T<X>`) may have only public methods.

### Enums

- Plain members may have integer values; a member without one takes the previous
  value plus one.
- Data-enum variants list fields, named (`Circle(radius: float)`) or not
  (`Ok(int)`: one unnamed field is `value`, several are `_0`, `_1`, ...).
- Methods follow the variants.

## Statements

```text
let name[: T] = value          const NAME[: T] = value
let (a, b) = pair              let [x, y] = fixedArray        let {x, y} = structValue
target = value                 target op= value               x++  x--  ++x  --x
expression
if (cond) { } [else if (cond) { }] [else { }]
while (cond) { }
do { } while (cond)
for (init; cond; update) { }
for (i in a..b [step s]) { }   for (i in a..=b) { }
for (x in xs) { }              for (i, x in xs) { }           for ((a, b) in pairs) { }
label: <loop>
break [label]                  continue [label]
return [value]
switch (value) { case v1, v2: body  case lo..hi: body  case x if cond: body  default: body }
match (value) { ... }          (a synonym of switch)
match (t) { case (0, y): body  case (x, _) if x > y: body  case _: body }   (t a tuple)
match (a, b) { ... }           (matches the tuple (a, b))
throw value
try { } catch (e: Type) { } [catch (e: Other) { }] [finally { }]
defer statement                defer { statements }
fn name(params) -> R { }       (a local function)
{ statements }                 (a nested block with its own scope)
%{ C statements %};
```

### Rules

- Conditions must be `bool`.
- A name is declared once per block; inner blocks may shadow outer names.
- Unused local variables are warnings.
- `switch` cases never fall through. A `switch` over an enum without `default`
  warns about missing members; a `match` over a data enum without `default` must
  cover every variant (an error otherwise).
- A tuple pattern `(p1, p2[, p3[, p4]])` matches a `Pair`, `Triple` or `Quad`
  element by element: `_` matches anything, a name binds that element, a value
  or range (`0`, `"go"`, `Color.Red`, `1..=3`) must equal or contain it, and a
  nested tuple pattern matches a tuple element. A guard may use the names. Every
  case of such a `match` is a tuple pattern (or `case _:`/`default:`); a case
  of several patterns binds nothing; a case after one that matches every tuple is
  an error, and a tuple `match` used as a value needs one.
- In a loop, `break` inside a `case` leaves the loop.
- A case body runs from its colon to the next `case`, `default` or `}`; it
  cannot be empty (`case 1, 2:` gives one body several values).
- `for (x in v)` walks a fixed array, slice, `string` (characters), `binary`
  (bytes), or an object: by its `iterator()` when that gives something with
  `hasNext() -> bool` and `next() -> T`; else by `size()`/`length()` and
  `get(int)`/`at(int)`; else by its own `hasNext()` and `next()`.
- `defer`red code runs when its block exits (not when an exception passes
  through), in reverse order; it cannot `return`, `break` or `continue`.
- `finally` runs on every exit, including exceptions.
- A `catch` that can never run, because an earlier one catches a parent class, is an error.
- Local functions see variables declared before them (like a lambda), may call
  themselves, and cannot have defaults. A generic one (`fn pair<T>(x: T)`) is a
  hidden top-level function that each call passes the block variables it uses:
  it reads their values at the call and cannot assign them, and a call where one
  of them is shadowed by another variable of that name is an error.

## Expressions that contain statements

```text
match (value) { case a: expr1  case b, c: expr2  default: expr3 }
if (cond) value1 else if (cond2) value2 else value3
try value catch fallback       try value catch (e: Type) fallback
fn(params) -> R { statements }
```

Each value or fallback of `if` and `try` is an expression, or a `{ block }` whose
last statement is the value. An `if` value needs its `else`, a `try` value a
`catch`; a `try` value has no `finally`. A line that starts with `(` (or an
interpolated string) begins a new statement: it never calls the previous line's
value.

A `match` expression needs a `default` unless its cases cover every enum member
or data-enum variant.
