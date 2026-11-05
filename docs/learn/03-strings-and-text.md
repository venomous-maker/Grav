# 3. Strings and text

Text is in nearly every program: names, messages, file contents, network
requests. Grav's `string` type holds UTF-8 text and cannot be changed in place:
every operation that "changes" a string gives you a new one. That makes strings
safe to share between variables, objects and threads.

In this chapter you will learn:

- how to write string literals, including escapes, raw strings and multi-line text,
- how to join strings and put values inside them,
- how to format numbers and line up columns,
- what a string really is (bytes) and how to work with its characters,
- where to find everything else: the `Str` and `StringBuilder` classes.

## String literals

A string literal is text between double quotes. Inside it, a backslash starts an
*escape sequence* for a character you cannot type directly:

```grav
fn main() {
    print("one\ttwo")             // a tab
    print("line 1\nline 2")       // a line break
    print("she said \"hi\"")      // quotes inside quotes
    print("C:\\path")             // a backslash
    print("\u{2764} \x41")        // a Unicode code point, and one byte (A)
}
```

Output:

```text
one	two
line 1
line 2
she said "hi"
C:\path
❤ A
```

| Escape | Meaning |
|--------|---------|
| `\n` `\t` `\r` | line break, tab, carriage return |
| `\\` `\"` `\'` | backslash, double quote, single quote |
| `\$` | a literal `$` (so `\${x}` is not interpolated) |
| `\0` `\a` `\b` `\f` `\v` | control characters |
| `\e` | the escape character, for terminal colours |
| `\xHH` | one byte, written in hex: `\x41` is `A` |
| `\u{1F680}` | any Unicode character by its code point |

### Raw strings

When a string is full of backslashes, such as a Windows path or a regular
expression, escapes get in the way. Put an `r` in front for a *raw* string, where
nothing is special:

```grav
fn main() {
    print(r"C:\temp\new")
    print(r"\d+\.\d+")
    print(r"costs ${price}")      // not interpolated either
}
```

Output:

```text
C:\temp\new
\d+\.\d+
costs ${price}
```

A raw string ends at the next `"`, so it cannot contain one. Use a raw
multi-line string (below) for that.

### Multi-line strings

Three double quotes start a string that can span lines. It is made for text
blocks that you want to indent along with your code:

```grav
fn main() {
    let name = "Ada"
    let letter = """
        Dear ${name},

          Thank you for your "kind" letter.
        Regards
        """
    print(letter)
}
```

Output:

```text
Dear Ada,

  Thank you for your "kind" letter.
Regards
```

The rules:

- A line break right after the opening `"""` is dropped.
- The indentation that all lines share is removed. When the closing `"""` is on
  its own line, its indentation decides how much is removed, and the line break
  before it is dropped too. Lines indented further keep the extra spaces.
- Quotes need no escaping, and escapes and `${...}` work as usual.
- `r"""..."""` is a raw multi-line string: no escapes, no interpolation.

## Joining strings and interpolation

`+` joins two strings, and `+=` adds to the end of a string variable:

```grav
fn main() {
    let first = "Grace"
    let last = "Hopper"
    let full = first + " " + last
    full += "!"
    print(full)
}
```

Output:

```text
Grace Hopper!
```

`+` only joins strings with strings. To put a number or any other value into a
string, use *interpolation*: `${expression}` inside a string is replaced by the
value of the expression, whatever its type.

```grav
fn main() {
    let item = "apples"
    let count = 3
    let price = 0.5
    print("${count} ${item} cost ${count * price}")
    print("Is it cheap? ${price < 1.0}")
}
```

Output:

```text
3 apples cost 1.5
Is it cheap? true
```

Any expression works inside `${}`, including function calls and conditionals.
`str(value)` gives the same text as `"${value}"`.

## Formatting

Add `:spec` after an interpolated value to control how it looks:

```grav
fn main() {
    let price = 4.5
    print("${price:.2f}")       // 2 decimal places
    print("${0.257:.1%}")       // as a percentage
    print("${255:x} ${255:X} ${5:b}")   // hex, HEX, binary
    print("[${7:3}] [${7:03}] [${7:<3}] [${7:^5}]")
    print("[${"Grav":>8}] [${"Grav":<8}] [${"Grav":.2}]")
}
```

Output:

```text
4.50
25.7%
ff FF 101
[  7] [007] [7  ] [  7  ]
[    Grav] [Grav    ] [Gr]
```

The spec is `[align][+][0][width][.precision][type]`:

| Part | Meaning |
|------|---------|
| align | `<` left, `>` right, `^` centre. Numbers go right and text left by default. |
| `+` | always show the sign of a number |
| `0` | pad numbers with zeros instead of spaces |
| width | the minimum width, padded with spaces |
| .precision | decimals for `f`, `e` and `%`; for text, the maximum length |
| type | `d` integer, `f` fixed-point, `e` exponent, `%` percent, `x`/`X` hex, `o` octal, `b` binary, `s` text |

Widths make tables easy:

```grav
import "std"

fn main() {
    let names: Array<string> = ["tea", "coffee", "hot chocolate"]
    let prices: Array<float> = [2.5, 3.25, 4.0]
    for (i in 0..names.size()) {
        print("${names[i]:<14}${prices[i]:>6.2f}")
    }
}
```

Output:

```text
tea             2.50
coffee          3.25
hot chocolate   4.00
```

The compiler checks specs it can: `${name:x}` on a string is an error, because
hex only makes sense for integers.

## Comparing strings

`==` and `!=` compare the text, not where it is stored. `<`, `>`, `<=` and `>=`
compare byte by byte, which is alphabetical order for plain ASCII:

```grav
fn main() {
    let a = "apple"
    print(a == "app" + "le")
    print("apple" < "banana")
    print("Zebra" < "apple")    // capital letters sort before lowercase
}
```

Output:

```text
true
true
true
```

For case-insensitive comparison, use `Str.equalsIgnoreCase(a, b)` or
`Str.compareIgnoreCase(a, b)`.

## What a string is made of

A string is a sequence of bytes in UTF-8. English letters and digits take one
byte each, but most other characters take two to four. That affects what
`length` and indexing mean:

```grav
fn main() {
    let word = "héllo"
    print(word.length)      // bytes: é takes two
    print(word[0])          // the first byte, as a number
    let n = 0
    for (ch in word) { n++ }
    print(n)                // characters
}
```

Output:

```text
6
104
5
```

- `s.length` is the number of **bytes**.
- `s[i]` is the **byte** at index `i`, as a `byte` (a small number). Indexes run
  from 0 to `length - 1`; anything else is an `IndexError`.
- `for (ch in s)` visits each **character**, as a one-character string.
- `for (i, ch in s)` also gives the byte position where each character starts.

### Character literals

`'a'` is the code point of a single character, as an `int`. It is handy for
comparing bytes without remembering their numbers:

```grav
import "std"

fn main() {
    let code = "a1b22c"
    let digits = 0
    for (i in 0..code.length) {
        if (code[i] >= '0' && code[i] <= '9') { digits++ }
    }
    print(digits)
    print('A')                  // a number: 65
    print('A' + 2)
    print(Str.fromChar('A' + 2))   // back to text
}
```

Output:

```text
3
65
67
C
```

Character literals take the same escapes as strings: `'\n'`, `'\''`, `'\\'`,
`'\u{1F600}'`. A character literal holds exactly one character; `'ab'` is an
error (use `"ab"`).

### Slices

`s[lo..hi]` is a new string with the bytes from `lo` up to, not including, `hi`.
`..=` includes `hi`. Leave out either end to mean the start or the end. Bounds
past the string are cut to fit:

```grav
fn main() {
    let s = "hello world"
    print(s[0..5])
    print(s[6..])
    print(s[..=4])
    print(s[6..100])
}
```

Output:

```text
hello
world
hello
world
```

## The `Str` class

Everything else you would want to do with text lives in the standard library's
`Str` class, as static functions that take the string first:

```grav
import "std"

fn main() {
    let line = "  Grav,Ada,  Hopper  "
    let trimmed = Str.trim(line)
    print(trimmed)
    let parts = Str.split(trimmed, ",")
    print(parts)
    print(Str.join(parts, " | "))
    print(Str.upper("loud"))
    print(Str.contains(line, "Ada"))
    print(Str.replace("a-b-c", "-", "+"))
    print(Str.padLeft("42", 5, "0"))
    print(Str.indexOf("banana", "na"))
}
```

Output:

```text
Grav,Ada,  Hopper
["Grav", "Ada", "  Hopper"]
Grav | Ada |   Hopper
LOUD
true
a+b+c
00042
2
```

There are around fifty of them: searching (`indexOf`, `contains`, `startsWith`,
`count`), changing case, trimming and padding, splitting into lines and words,
checking contents (`isDigits`, `isBlank`) and more. See the full list in
[`Str`](../std/text/string.md).

## Building long strings

Each `+` creates a new string. When you build text in a loop, a
`StringBuilder` is faster: it grows one buffer and turns it into a string once at
the end.

```grav
import "std"

fn main() {
    let sb = new StringBuilder()
    for (i in 1..=5) {
        if (i > 1) { sb.append(", ") }
        sb.append(i * i)
    }
    print(sb.toString())
}
```

Output:

```text
1, 4, 9, 16, 25
```

See [`StringBuilder`](../std/text/strbuf.md) for everything it can do.

## Strings and numbers

`str(x)` turns any value into text. Going the other way needs care, because the
text might not be a number. The standard library's `Format` class does the
parsing. Check the text first with `Format.isInt` (or `isNumber`), or give a
fallback for bad input with `toIntOr`:

```grav
import "std"

fn main() {
    print(Format.toInt("42") + 1)
    print(Format.toFloat("2.5") * 2)
    let answer = "forty-two"
    if (Format.isInt(answer)) {
        print(Format.toInt(answer))
    } else {
        print("not a number: ${answer}")
    }
    print(Format.toIntOr("oops", -1))
}
```

Output:

```text
43
5
not a number: forty-two
-1
```

`Format.toInt` reads the leading digits and ignores the rest (`"12a"` is 12, and
text with no digits is 0), so do not use it alone on input you do not trust.
See [`Format`](../std/text/convert.md).

## Summary

- Strings are immutable UTF-8 text. `+` joins them, `${...}` puts any value
  inside them, and `${x:spec}` formats it.
- `r"..."` is raw, `"""..."""` spans lines and loses its shared indentation.
- `length` and `s[i]` work in bytes; `for (ch in s)` works in characters.
- `'a'` is a character's code point, an `int`.
- `Str` has the text functions, `StringBuilder` builds long strings, and
  `Convert` turns text into numbers.

## Exercises

1. Print a multiplication table for 1 to 9, with every number right-aligned in a
   column of width 3.
2. Count the vowels in a string using a `for` loop over its characters.
3. Write a program that reverses the words of `"one two three"` using
   `Str.split` and `Str.join`. (You can build a new array by looping backwards.)
4. Print a price of `1234.5` as `$1234.50`.
5. Make a small text box: print `"+------+"`, `"| Grav |"`, `"+------+"` using a
   multi-line string, then using `Str.repeat` for the dashes.

Next: [Control flow →](04-control-flow.md)
