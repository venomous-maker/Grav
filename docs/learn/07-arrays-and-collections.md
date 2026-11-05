# 7. Arrays and collections

Most programs work with many values at once: the lines of a file, the users of
a site, the scores of a game. This chapter covers the two ways Grav stores a
group of values: *fixed arrays*, which are built into the language and never
change size, and the standard library's *collections*, which grow and shrink as
you need.

In this chapter you will learn:

- fixed-length arrays `T[N]`, and indexing and slicing,
- `Array<T>`, the growable list you will use most,
- maps (`HashMap`) for looking things up by key,
- sets (`HashSet`) for "have I seen this?",
- collection literals: `[1, 2, 3]` and `{"key": value}`,
- how to choose a collection.

## Fixed arrays

`[1, 4, 9, 16]` is an *array literal*. On its own, it makes a fixed array: a
row of values of one type, whose length never changes. Its type is written
`T[N]`: `int[4]` here.

```grav
fn main() {
    let squares = [1, 4, 9, 16]
    print(squares[0])         // the first element
    print(squares.length)
    squares[0] = 0            // elements can be replaced
    let sum = 0
    for (v in squares) { sum += v }
    print(sum)
    print(squares)
}
```

Output:

```text
1
4
29
[0, 4, 9, 16]
```

- Elements are numbered from **0**: the first is `a[0]`, the last is
  `a[a.length - 1]`.
- Every index is checked. Using an index outside `0..length` stops with an
  `IndexError` instead of reading random memory, as C would.
- Fixed arrays are *values*, like numbers: assigning one to another variable, or
  passing it to a function, copies all the elements.

Arrays can hold arrays, which gives you a grid. `int[3][2]` is 2 rows of 3:

```grav
fn main() {
    let grid: int[3][2] = [[1, 2, 3], [4, 5, 6]]
    print(grid[1][2])
    for (row in grid) {
        let line = ""
        for (cell in row) { line += "${cell} " }
        print(line)
    }
}
```

Output:

```text
6
1 2 3 
4 5 6 
```

Fixed arrays are fast and simple, but their size is part of their type. When you
do not know in advance how many values there will be, you need a collection.

## Array: the growable list

`Array<T>` from the standard library is a list of `T` that grows as you add to
it. `<T>` is a *type argument*: `Array<string>` holds strings, `Array<int>`
holds ints. (You will write your own types like this in [Generics](13-generics.md).)

```grav
import "std"

fn main() {
    let todo = new Array<string>()
    todo.push("write docs")
    todo.push("fix bug")
    todo.push("release")
    print(todo.size())
    print(todo[1])
    todo[1] = "fix two bugs"
    todo.removeAt(0)
    print(todo)
    for (i, task in todo) {
        print("${i + 1}. ${task}")
    }
}
```

Output:

```text
3
fix bug
["fix two bugs", "release"]
1. fix two bugs
2. release
```

`new Array<string>()` creates an empty array. Like fixed arrays, `Array` is
indexed with `[i]` from 0, every index is checked, and `for` loops over it.
Unlike them, it is an *object*: assigning it or passing it to a function
shares the same array, it does not copy it.

The methods you will use most:

| Method | Does |
|--------|------|
| `push(x)` / `add(x)` | adds `x` at the end |
| `insert(i, x)` | inserts `x` at index `i`, moving the rest along |
| `get(i)`, `xs[i]` | the element at `i` |
| `set(i, x)`, `xs[i] = x` | replaces the element at `i` |
| `removeAt(i)` | removes and returns the element at `i` |
| `remove(x)` | removes the first element equal to `x` |
| `pop()` | removes and returns the last element |
| `size()` | the number of elements |
| `isEmpty()`, `clear()` | is it empty? / remove everything |
| `contains(x)`, `indexOf(x)` | searching (`indexOf` gives -1 when absent) |
| `first()`, `last()` | the ends |
| `slice(from, to)`, `xs[from..to]` | a new array with part of the elements |
| `reverse()`, `sortBy(cmp)` | reorder in place |

Plus the lambda methods from the last chapter (`map`, `filter`, `reduce`, ...).
The full list is in [`Array`](../std/collections/array.md).

## Collection literals

Writing `push` for every starting element is tedious. Where the compiler knows
a collection is expected, `[...]` builds that collection directly:

```grav
import "std"

fn total(xs: Array<int>) -> int {
    let t = 0
    for (x in xs) { t += x }
    return t
}

fn main() {
    let primes: Array<int> = [2, 3, 5, 7]
    primes.push(11)
    print(primes)
    print(total([10, 20, 30]))      // the parameter says Array<int>
}
```

Output:

```text
[2, 3, 5, 7, 11]
60
```

"Where a collection is expected" means: a variable or field declared with that
type, a parameter of that type, or a `return` from a function that returns it.
Without that, `[...]` stays a fixed array. The same works for any collection
class with an empty constructor and an `add` or `push` method, including your own.

## Maps: looking things up by key

A map stores values under keys: a phone book maps names to numbers, a word
count maps words to counts. `HashMap<K, V>` maps keys of type `K` to values of
type `V`:

```grav
import "std"

fn main() {
    let ages = {"ann": 31, "bob": 27}     // a map literal
    ages["cy"] = 45                        // add or replace
    print(ages["bob"])
    print(ages.size())
    print(ages.containsKey("dan"))
    print(ages.getOr("dan", -1))
    ages.remove("ann")
    for (name in ages.keys()) {
        print("${name} is ${ages[name]}")
    }
}
```

Output:

```text
27
3
false
-1
bob is 27
cy is 45
```

- `{key: value, ...}` is a map literal; it builds a `HashMap`. An empty map is
  written `new HashMap<string, int>()`.
- `map[key] = value` adds the key or replaces its value.
- `map[key]` gives the value. For a missing key it gives the *zero value* of the
  value type (`0`, `""`, `false` or `null`), so use `containsKey` or
  `getOr(key, fallback)` when the key might be missing.
- A `HashMap` does not keep keys in any particular order. Use `Map` to keep
  insertion order, or `SortedMap` to keep keys sorted.

Counting things is the classic use:

```grav
import "std"

fn main() {
    let text = "the cat and the hat and the bat"
    let counts = new HashMap<string, int>()
    for (word in Str.split(text, " ")) {
        counts[word] = counts.getOr(word, 0) + 1
    }
    print(counts["the"])
    print(counts["and"])
}
```

Output:

```text
3
2
```

Keys can be numbers, booleans, strings, enums or objects (objects are compared
by identity: the same object, not an equal one).

## Sets: is it in there?

A set holds each value at most once, and answers "is this in the set?" quickly.
`HashSet<T>` is the usual choice:

```grav
import "std"

fn main() {
    let seen = new HashSet<string>()
    for (name in ["ann", "bob", "ann", "cy", "bob"]) {
        if (!seen.add(name)) {
            print("duplicate: ${name}")
        }
    }
    print(seen.size())
    print(seen.contains("cy"))
}
```

Output:

```text
duplicate: ann
duplicate: bob
3
true
```

`add` returns `false` when the value was already there. Sets also do the
classic set operations: `union`, `intersect`, `difference`, `isSubsetOf`.

## Choosing a collection

| You need | Use |
|----------|-----|
| a list in order, indexed by position | `Array<T>` |
| to add and remove at both ends | `Deque<T>` |
| last in, first out | `Stack<T>` |
| first in, first out | `Queue<T>` |
| to look values up by a key | `HashMap<K, V>` |
| ... and keep keys sorted | `SortedMap<K, V>` |
| ... and keep insertion order (small maps) | `Map<K, V>` |
| to know whether you have seen a value | `HashSet<T>` |
| always take the smallest (or most urgent) next | `PriorityQueue<T>` |
| a set of small non-negative numbers | `BitSet` |
| two or three values together | `Pair<A, B>`, `Triple<A, B, C>` |

All of them share the same basic method names (`size()`, `isEmpty()`,
`clear()`, `contains()`, `forEach()`, `toArray()`), and all print their
contents with `print`. The [standard library overview](../std/README.md#collections)
introduces each one.

## Summary

- `[1, 2, 3]` is a fixed array (`int[3]`): a value, fixed size, indexed from 0,
  bounds-checked.
- `Array<T>` grows and shrinks; `push`, `[i]`, `removeAt`, `size()` and `for` cover most uses.
- Where a collection is expected, `[...]` builds it; `{k: v}` builds a `HashMap`.
- `HashMap<K, V>` looks values up by key; missing keys give the zero value, so
  use `getOr` or `containsKey`.
- `HashSet<T>` holds each value once.
- Collections are objects: passing one shares it.

## Exercises

1. Read numbers from an `Array<int>` literal and print the largest and the
   smallest without using `sortBy`.
2. Remove every negative number from an `Array<int>`. (Look at `removeWhere`, or
   build a new array with `filter`.)
3. Count how many times each letter appears in `"mississippi"` with a
   `HashMap<string, int>`, then print the counts.
4. Given two arrays of names, print the names that appear in both, using a `HashSet`.
5. Make a 3×3 tic-tac-toe board as a `string[3][3]` filled with `"."`, put an
   `"X"` in the middle and print the board.

Next: [Structs, enums and tuples →](08-structs-enums-tuples.md)
