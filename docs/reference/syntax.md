# Syntax and keywords

The lexical rules of Grav: how source text is split into comments, names,
keywords and literals.

## Source files

- Source files are UTF-8 text with the extension `.grav`.
- A file is a list of top-level declarations: functions, classes, structs,
  enums, interfaces, traits, type aliases, global `let`/`const`, namespaces,
  `import`s, `use`s, tests, `#define`s and inline C blocks. Their order does not
  matter.
- A program starts at `fn main()`, which takes no parameters and returns nothing.

## Comments

```text
// to the end of the line
/* anything, across lines */
```

A `//` comment directly above a declaration (or at the end of its line) is its
*doc comment*: editors show it on hover.

## Statements and line breaks

A statement ends at the end of its line, at a `;`, or at a closing `}`. Use `;`
to put several statements on one line. A line break does **not** end a statement
when the line ends with a binary operator, a `,`, or an opening bracket, so long
expressions can be split after an operator.

A statement that *starts* with `*` or `&` (a pointer dereference or address)
could be read as continuing the previous line, so end the previous statement
with `;` in that case:

```text
bump(&c);
*p = 1
```

## Names

Identifiers start with a letter or `_` and continue with letters, digits and
`_`. They are case-sensitive. Conventions: `UpperCamelCase` for types,
`lowerCamelCase` for functions, methods, variables and fields,
`UPPER_CASE` for constants.

Names starting with `__` are reserved for the compiler and standard library.

## Keywords

These words are reserved and cannot be used as names:

| Keyword | Meaning | See |
|---------|---------|-----|
| `let`, `const` | declare a variable / a constant | [Values](../learn/02-values-and-types.md#variables) |
| `fn` | declare a function, method, lambda or function type | [Functions](../learn/05-functions.md) |
| `return` | leave a function, optionally with a value | |
| `if`, `else` | conditional | [Control flow](../learn/04-control-flow.md) |
| `while`, `do` | loops | |
| `for`, `in` | C-style, range and for-each loops | |
| `break`, `continue` | leave / skip ahead in a loop | |
| `switch`, `match`, `case`, `default` | multi-way branch and pattern matching | |
| `class`, `struct`, `enum`, `interface`, `trait` | type declarations | [Classes](../learn/10-classes.md) |
| `type`, `typedef` | type alias | [Aliases](../learn/08-structs-enums-tuples.md#type-aliases) |
| `abstract` | a class that cannot be created / a method without a body | [Inheritance](../learn/11-inheritance-and-interfaces.md) |
| `extends`, `implements`, `uses` | parent classes, interfaces, composition | |
| `constructor` | a class's initialiser | |
| `new` | create an object | |
| `self`, `this` | the current object | |
| `super` | the parent class's constructor or methods | |
| `static` | a member of the class rather than of each object | |
| `readonly` | a field set only in constructors | |
| `public`, `private`, `protected` | access control | |
| `namespace` | group declarations under a name | [Modules](../learn/16-modules-and-packages.md) |
| `export` | mark a declaration as public (documentation only) | |
| `try`, `catch`, `finally`, `throw` | exceptions | [Errors](../learn/14-errors.md) |
| `async`, `await` | background tasks | [Concurrency](../learn/18-concurrency.md) |
| `as`, `is` | conversion and type test | |
| `sizeof` | size of a type in bytes | [C interop](../learn/17-c-interop.md) |
| `true`, `false`, `null` | literals | |
| `int`, `float`, `bool`, `string`, `void` | built-in types | [Types](types.md) |

### Contextual keywords

These are special only in certain positions and remain usable as names
elsewhere:

| Word | Special where |
|------|---------------|
| `import`, `from` | at the top level: `import "path"`, `import { A } from "path"` |
| `use` | at the top of a namespace or file |
| `test` | `test "name" { ... }` at the top level |
| `defer` | at the start of a statement |
| `step` | in a range: `for (i in 0..10 step 2)` |
| `final` | before `class` or `fn` |
| `destructor` | in a class body |
| `operator` | in `fn operator+(...)` |
| `weak` | before a field |
| `auto` | as a type argument, or `let x: auto` |
| `any` | as a type |

The built-in type names `long`, `int8` … `int64`, `uint`, `unsigned`, `ulong`,
`uint8` … `uint64`, `byte`, `double`, `float32`, `float64` and `binary` are also
valid identifiers outside type positions.

## Literals

| Kind | Examples | Type |
|------|----------|------|
| integer | `42`, `-7`, `1_000_000`, `0xFF`, `0b1010` | `int`, or `long` if it does not fit |
| float | `3.14`, `0.5`, `1.5e3`, `2.0` | `float` |
| boolean | `true`, `false` | `bool` |
| null | `null` | references and `T?` |
| string | `"text"`, `"tab\t"`, `"sum=${a + b}"`, `"${x:.2f}"` | `string` |
| raw string | `r"C:\dir"`, `r"\d+"` | `string` |
| multi-line string | `"""` ... `"""` (shared indentation removed) | `string` |
| raw multi-line string | `r"""` ... `"""` | `string` |
| character | `'a'`, `'\n'`, `'\u{1F600}'` | `int` (the code point) |
| fixed array | `[1, 2, 3]`, `[[1, 2], [3, 4]]` | `int[3]`, `int[2][2]` |
| collection | `[1, 2, 3]` where a collection is expected | that collection |
| map | `{"a": 1, "b": 2}` | `HashMap<string, int>` |
| struct | `Point { x: 1, y: 2 }` | `Point` |
| tuple | `(1, "one")` | `(int, string)`, the std `Pair` |
| lambda | `fn(x) => x * 2`, `fn(a: int) -> int { return a }` | a function type |

A leading `0` makes an integer octal, as in C (`017` is 15).

### Escape sequences

In strings and character literals:

| Escape | Meaning |
|--------|---------|
| `\n` `\t` `\r` | line feed, tab, carriage return |
| `\\` `\"` `\'` | backslash, double quote, single quote |
| `\$` | a literal `$` (prevents interpolation) |
| `\0` `\a` `\b` `\f` `\v` | control characters |
| `\e` | escape (27), for terminal colours |
| `\xHH` | one byte |
| `\u{H...}` | a Unicode code point, encoded as UTF-8 |

Raw strings have no escapes.

### Interpolation and format specs

`${expression}` inside a (non-raw) string inserts the expression's text.
`${expression:spec}` formats it, where spec is
`[align][+][0][width][.precision][type]`:

| Part | Values |
|------|--------|
| align | `<` left, `>` right, `^` centre |
| `+` | always show the sign |
| `0` | pad numbers with zeros |
| width | minimum width |
| precision | decimals (`f`, `e`, `%`), or the maximum length of text |
| type | `d` `f` `e` `%` `x` `X` `o` `b` `s` |

## Preprocessor

`#define NAME text` and `#define NAME(a, b) text` define macros, which are
substituted before parsing. See [C interop](../learn/17-c-interop.md#macros).

## Inline C

`%{ ... %}` embeds C code: at the top level as declarations, as an expression
with `as T`, or as a statement ending in `;`. See
[C interop](../learn/17-c-interop.md#inline-c).
