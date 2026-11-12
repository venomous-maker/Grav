# Reflection

`import "std/core"` (included in `import "std"`) · namespace `std.core`

Reflection lets code work with classes it does not know in advance: an ORM that
maps rows to models, a serializer, a validator, a dependency injector.
`reflect(User)` (or `ClassInfo.of(anObject)`) gives a `ClassInfo` that lists a
class's fields, reads and writes them through `any`, creates objects, calls
methods by name, and returns the **decorators** written on the class and its
members, with their arguments.

```grav
import "std"

@Table("users")
class User {
    @Column("user_id") @PrimaryKey
    public id: int
    @Column("email_address") @MaxLength(255)
    public email: string
    public age: int?
    constructor() { self.id = 0; self.email = ""; self.age = null }
}

fn main() {
    let info = reflect(User)
    print(info.name + " -> table " + info.decorator("Table").text(0))
    for (f in info.fields()) {
        let col = f.decorator("Column")
        print("  " + f.toString() + (col == null ? "" : " as " + col.text(0)))
    }
    // A row from a database, into a new User.
    let row = new HashMap<string, any>()
    row["user_id"] = 7
    row["email_address"] = "ada@example.com"
    let u = new User()
    for (f in info.fields()) {
        let c = f.decorator("Column")
        let key = c == null ? f.name : c.text(0)
        if (row.containsKey(key)) { f.set(u, row[key]) }
    }
    print("${u.id} ${u.email} ${u.age}")
    print(info.field("email").decorator("MaxLength").number(0, 0))
    print(ClassInfo.of(u) == info)
}
```

Output:

```text
User -> table users
  id: int as user_id
  email: string as email_address
  age: int?
7 ada@example.com null
255
true
```

- **Decorators** go on classes, fields, methods and constructors: `@Name` or
  `@Name(args)`. Arguments are literals (strings, numbers, `true`/`false`) or
  names (`@HasMany(Post)` gives the text `"Post"`). The names are yours: the
  compiler only records them. `text(i)`, `number(i, fallback)` and `arg(i)` read
  the arguments.
- **Fields** come base class first, with private and readonly fields included
  (reflection is for library code). `get(obj)` boxes the value; `set(obj, v)`
  takes an `any` that fits the field, **converting** it when it can (what a
  database row holds rarely matches a model's types exactly):
  - an integer field (`int`, `long`, `byte`, ...) takes a whole number in its
    range: an integer, a bool, a float without a fraction (`12.0`), or a string
    holding one (`"42"`, as databases give DECIMAL and BIGINT);
  - a float field takes any number or a numeric string (`"19.99"`);
  - a bool field takes a bool, `0` or `1`, or `"true"`/`"false"`, `"t"`/`"f"`,
    `"yes"`/`"no"`, `"1"`/`"0"`;
  - a string field takes a string; a class field an object of the class (or
    `null`); a `T?` field `null` or what a `T` field takes; an enum field the
    enum or its number.

  Anything else is a `ValueError` naming the field, its type and the value
  (`field 'age' of User (int) cannot hold "4x"`). Interface and function fields
  can be read but not written, and value structs and fixed arrays neither
  (`canGet()`, `canSet()`).
- **Creating objects.** `info.create()` is `new User()`, and
  `info.create(["Ada", 36])` calls the constructor taking two arguments that
  they fit (converted as `set` converts). `constructors()` lists them and
  `canCreate()` says whether one takes no arguments. An abstract class has none.
  This is what a generic repository needs: inside `class Repository<T>`,
  `reflect(T)` is the class `T` stands for in each use (`Repository<User>`
  reflects `User`), so it can make a `T` without being handed a factory.
- **Calling methods.** `info.invoke(obj, "greet", ["Bob"])` is
  `obj.greet("Bob")`: of the methods called `greet` (the class's own, then its
  base classes'), the one taking that many arguments that they fit. An
  overridden method runs the object's own version, as a normal call does. A
  static method takes `null` for `obj`. `method(name)` gives the `MethodInfo`,
  whose `invoke(obj, args)` calls that one method; `paramCount()` and `params`
  (`"string, int"`) describe it. Its result comes back as an `any` (`null` for
  none), and what it throws goes on. A method can be called this way when its
  parameters and result can go through an `any` (numbers, bools, strings,
  enums, objects, `T?`, `any`); one taking a function, say, cannot
  (`canInvoke()`).
- **In a static method**, `reflect(Self)` is the class the call names (see
  [Self](../../learn/11-inheritance-and-interfaces.md#self-methods-and-statics-that-fit-every-subclass)),
  so a base class can hydrate any model: `User.hydrate(row)` makes a `User`.
- **What it costs.** The compiler records a class only when its hierarchy (its
  root class and everything under it) has a decorator somewhere, or
  `reflect(...)` names one of its classes; other classes have no tables, so
  programs that do not use reflection are unchanged. A recorded class costs
  about 250 bytes of code and 90 bytes of static tables per field, and nothing
  at run time until it is first reflected. There is one `ClassInfo` per class,
  made once, shared by every thread and never changed. A reflective read or
  write takes about 40 ns (a plain field access is under 1 ns), and finding a
  `ClassInfo` again is one atomic load.
- `ClassInfo.of(x)` is `null` when `x` holds no object; for a class without
  recorded metadata it still knows the name and base, with no fields or methods
  (`isReflected()` is false), and it cannot be created or its methods called.

## API

<!-- api:begin (generated by docs/std/gen-api.js from the library source; do not edit) -->

### class Decorator

A decorator as written: `@Column("email", 255)` has the name "Column" and
the arguments "email" and 255 (a name argument, `@HasMany(Post)`, is text).

**Constructors**

- `new Decorator(name: string)`

**Fields**

- `name: string`
- `args: Array<any>`

**Methods**

- `fn arg(i: int) -> any` — Argument i, or null when there is no such argument.
- `fn text(i: int) -> string` — Argument i as text ("" when missing).
- `fn number(i: int, fallback: long) -> long` — Argument i as a number (`fallback` when missing or not a whole number).
- `fn toString() -> string`

### class FieldInfo

One field of a class: its name, type and decorators, and a way to read and
write it on any object of that class.

**Constructors**

- `new FieldInfo(meta: byte*, owner: byte*)`

**Fields**

- `name: string` — The field's name.
- `typeName: string` — Its type as written in Grav ("int", "string", "User", ...).
- `declaringClass: string` — The class that declares it ("app.User").
- `decorators: Array<Decorator>` — Its decorators, in source order.

**Methods**

- `fn isReadonly() -> bool`
- `fn isPrivate() -> bool`
- `fn isProtected() -> bool`
- `fn isWeak() -> bool`
- `fn canGet() -> bool` — Whether get() works for its type (numbers, bools, strings, enums, objects, any, functions, T?).
- `fn canSet() -> bool` — Whether set() works for its type (as canGet, but not interfaces or functions).
- `fn has(name: string) -> bool` — Whether it has a decorator called `name`.
- `fn decorator(name: string) -> Decorator` — Its first decorator called `name`, or null.
- `fn get(obj: any) -> any` — The field's value in `obj`, an object of the declaring class or a subclass (ValueError otherwise).
- `fn set(obj: any, value: any)` — Stores `value` in the field of `obj`. ValueError when the value does not fit its type, converted as the top of this file says (an int field takes 42, 42.0, true or "42" when in range; a float field any number or numeric string; a bool field true, 1 or "t"), a class field an object of that class or null, a T? field a T or null. Readonly and private fields are written too.
- `fn toString() -> string`

### class MethodInfo

One method of a class as written: its name, whether it is static, and its decorators.

**Constructors**

- `new MethodInfo(meta: byte*, owner: string, kind: int)`

**Fields**

- `name: string`
- `decorators: Array<Decorator>`
- `declaringClass: string` — Its class ("app.User").
- `params: string` — Its parameter types as written ("string, int").

**Methods**

- `fn paramCount() -> int` — How many arguments it takes.
- `fn canInvoke() -> bool` — Whether invoke can call it: every parameter and its result can go through an `any` (numbers, bools, strings, enums, objects, `T?`, any).
- `fn invoke(obj: any, args: Array<any> = new Array<any>()) -> any` — Calls it on `obj` (ignored for a static method) with `args`, each converted to its parameter's type; its result, or null when it returns nothing. What the method throws goes on.
- `fn isStatic() -> bool`
- `fn isPrivate() -> bool`
- `fn isProtected() -> bool`
- `fn has(name: string) -> bool`
- `fn decorator(name: string) -> Decorator`
- `fn toString() -> string`

### class ClassInfo

What a class is at run time: reflect(User), or ClassInfo.of(anObject).

**Constructors**

- `new ClassInfo(t: byte*)`

**Static members**

- `static fn of(value: any) -> ClassInfo`

**Fields**

- `name: string` — The fully qualified name ("app.User").

**Methods**

- `fn simpleName() -> string` — The name without its namespace ("User").
- `fn base() -> ClassInfo` — The base class, or null.
- `fn isReflected() -> bool` — Whether the compiler recorded fields, methods and decorators for it.
- `fn decorators() -> Array<Decorator>` — Its own decorators (not its base classes').
- `fn has(name: string) -> bool`
- `fn decorator(name: string) -> Decorator`
- `fn fields() -> Array<FieldInfo>` — Every field, the base classes' first.
- `fn ownFields() -> Array<FieldInfo>` — The fields this class declares itself.
- `fn field(name: string) -> FieldInfo` — The field called `name` (its own or a base class's), or null.
- `fn methods() -> Array<MethodInfo>` — The methods this class declares itself.
- `fn method(name: string) -> MethodInfo` — The method called `name`: its own, else its base class's (the first overload; invoke picks among them). Null when there is none.
- `fn invoke(obj: any, name: string, args: Array<any> = new Array<any>()) -> any` — Calls method `name` on `obj` with `args`: the overload (its own, else a base class's) taking that many arguments that they fit.
- `fn constructors() -> Array<MethodInfo>` — Its constructors (none for an abstract class, or one without reflection).
- `fn canCreate() -> bool` — Whether create can make one with no arguments.
- `fn create(args: Array<any> = new Array<any>()) -> any` — A new object of the class (`new C(args...)`): the constructor taking that many arguments that they fit.
- `fn isSubclassOf(other: ClassInfo) -> bool` — Whether this class is `other` or extends it.
- `fn isInstance(value: any) -> bool` — Whether `value` holds an object of this class (or a subclass).
- `fn toString() -> string`

<!-- api:end -->
