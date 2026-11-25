# Versions and releases

The compiler and every package are released as **version tags** (`v1.7.0`),
each with a changelog entry and a GitHub release. Work in between lands on
`main`; users get it when it is released.

## Versions

Versions follow [semantic versioning](https://semver.org/), `MAJOR.MINOR.PATCH`:

| Change | Bump | Example |
|--------|------|---------|
| fixes only | patch | 1.7.0 → 1.7.1 |
| new language features, library APIs, commands | minor | 1.7.1 → 1.8.0 |
| working programs may stop compiling or behave differently | major | 1.8.0 → 2.0.0 |

`grav env` prints the compiler's version (`GRAV_VERSION`, set in
`CMakeLists.txt`). A package's version is its tag.

## Which compiler a package needs

A project or package says which compiler versions it builds with in its
`grav.toml`:

```toml
name = "grav-orbit"
lib = "src/orbit.grav"
grav = ">=1.7.0"
```

| Requirement | Means |
|-------------|-------|
| `">=1.7.0"` or `"1.7.0"` | 1.7.0 or newer |
| `"^1.7"` | 1.7.0 or newer, but below 2.0.0 |
| `"~1.7.2"` | 1.7.2 or newer, but below 1.8.0 |
| `">=1.7, <2"` | every clause must hold |
| `">1.7"`, `"<2"`, `"<=1.9"`, `"=1.7.2"` | as written |

A compiler that does not satisfy it stops with a clear message, both when
building and when `grav add`/`install`/`update` would fetch the package:

```text
grav-orbit needs grav >=1.8.0, and this is grav 1.7.0 (update it with: grav self-update)
```

Raise a package's requirement when it starts using something new: a language
feature, a std API, or a behaviour change. The changelog says which version
brought each one.

## Changelogs

Each repository has a `CHANGELOG.md`, newest release first. Changes go under
`## Unreleased` as they land:

```markdown
## Unreleased

### Fixes

- `HashMap.remove` during iteration no longer skips an entry.
```

Group them under `### Breaking changes` (always first, when there are any),
`### Language`, `### Standard library`, `### Fixes` and so on, and say what a
user notices, not how the code changed.

## Cutting a release

From a clean, up-to-date `main`:

```bash
tools/release.sh 1.8.0                    # the compiler
tools/release.sh 0.3.0 ../grav-orbit      # a package
tools/release.sh 1.8.0 --dry-run          # check and test, change nothing
```

`release.sh`:

1. checks the checkout: on `main`, nothing uncommitted, not behind `origin`,
   and the version newer than the last tag (a package must also have a `grav`
   requirement);
2. takes the release notes from the version's section of `CHANGELOG.md`, or
   turns `## Unreleased` into `## 1.8.0 (date)` above a fresh, empty
   `## Unreleased`;
3. sets `GRAV_VERSION` in `CMakeLists.txt` (the compiler);
4. runs the tests: `tests/run.sh` for the compiler, each `tests/*.grav` for a
   package (`--no-test` skips them);
5. commits `release: v1.8.0`, and makes an annotated tag carrying the notes;
6. asks, then pushes `main` and the tag and creates the GitHub release with
   `gh`.

## Releasing packages that depend on each other

When a package changes, every package that depends on it moves to the new tag
in its `grav.toml` and gets a release of its own, and so on up the chain. List
them dependencies first, one `<package> <X.Y.Z>` per line, and run:

```sh
tools/release-chain.sh plan.txt --base ~/code     # each package at ~/code/<package>
```

For each package it reinstalls the dependencies from their tags, so `grav.lock`
records the commits the releases before it just pushed. It commits the lock and
runs `release.sh`. It stops at the first failure, and `--from <package>` picks up
there.

## After a compiler release

`install.sh` installs the newest release tag, and `grav self-update` moves to
the newest one, so a release reaches users through those. Packages that use the
new features then raise their `grav` requirement in their next release.

An install built from your own checkout follows that checkout's branch instead
(`self-update` fast-forwards it), so working on the compiler never jumps to a
tag. `--ref` changes what an install follows:

```bash
grav self-update --ref latest    # the newest release (the default for a cloned install)
grav self-update --ref main      # every commit on main
grav self-update --ref v1.7.0    # stay on one release
```
