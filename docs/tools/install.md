# Installing Grav

Grav runs on **Linux** (Debian and Ubuntu are installed automatically; other
distributions work when the tools below are already there) and on **Windows
through WSL**. macOS and native Windows are planned. Docker images work
anywhere Docker does.

| System | How |
|--------|-----|
| Debian 12+, Ubuntu 22.04+ (also Kali, Mint, WSL) | `install.sh` |
| Other Linux | `install.sh`, after installing the tools it lists |
| Windows 10 2004+ / 11 | `install.ps1` (installs into WSL) |
| Anywhere with Docker | the `grav:debian` or `grav:ubuntu` image |

## Linux: install.sh

From a checkout:

```bash
./install.sh
```

Or without one (it clones the source first):

```bash
curl -fsSL https://raw.githubusercontent.com/venomous-maker/Grav/main/install.sh | bash
```

The script:

1. **checks the system**: Linux, the CPU, the distribution and its version,
   memory;
2. **checks what Grav needs**, and offers to install what is missing with apt:
   - to build the compiler: git, CMake 3.20 or newer, make, a C++20 compiler
     (g++ 10 or newer);
   - to use it: a C compiler with C headers (Grav programs are finished by
     `cc`, with pthreads and libm);
   - optional, loaded only by programs that use them: OpenSSL (https, wss),
     zlib, and the SQLite, PostgreSQL and MySQL/MariaDB client libraries
     (grav-db);
3. **asks** where to install (the defaults are shown; Enter takes them):

   | Question | Default |
   |----------|---------|
   | install prefix | `~/.local` (`/usr/local` as root) |
   | source checkout | this checkout, else `PREFIX/share/grav/src` |
   | package cache (`GRAV_PACKAGES`) | `~/.local/grav/packages` |
   | add grav to PATH in your shell's startup file | yes |
   | install the optional libraries | no |

4. **builds** the compiler (Release) and installs `PREFIX/bin/grav` and the
   standard library in `PREFIX/share/grav/lib`;
5. writes `PREFIX/share/grav/env.sh` (PATH and `GRAV_PACKAGES`) and loads it
   from `~/.bashrc`, `~/.zshrc` or `~/.profile`;
6. **builds and runs a test program** to prove it all works;
7. records the install in `PREFIX/share/grav/install.env`, for
   `grav self-update` and `uninstall.sh`.

Options:

| Option | Does |
|--------|------|
| `-y`, `--yes` | takes every default and asks nothing (for scripts and CI) |
| `--prefix DIR` | installs under `DIR` |
| `--src DIR` | builds the checkout in `DIR` (cloned there if missing) |
| `--ref REF` | what to build and follow: `latest` (the newest release tag), a tag, or a branch; default `latest` when cloning, the checked-out branch for your own checkout |
| `--repo URL` | clones from another repository |
| `--packages DIR` | sets the package cache |
| `--jobs N` | parallel build jobs (default: CPU cores) |
| `--optional` / `--no-optional` | installs the optional libraries / does not ask |
| `--no-path` | leaves your shell startup files alone |
| `--check` | only checks the system and lists what is missing |
| `--deps-only` | only installs what using grav needs (used by the Docker image) |

`./install.sh --check` is a quick way to see whether a machine is ready.

## Updating: grav self-update

```bash
grav self-update               # move to the newest release, if there is one
grav self-update --check       # only say whether an update is available
grav self-update --force       # rebuild even when nothing changed
grav self-update --ref main    # follow every commit on main from now on
grav self-update --ref v1.7.0  # stay on one release
```

An install `install.sh` cloned follows **releases** (`latest`: the newest
`v*` tag). One built from your own checkout follows that checkout's branch,
fast-forward only: a checkout with uncommitted changes or commits of its own
is left alone and reported. Either way it runs the new `install.sh` and lists
the commits it brought in, with the old and new versions.
[Versions and releases](releasing.md) explains how releases are made. The new binary is
moved into place by renaming, so a grav that is running meanwhile keeps
working.

Update the compiler **before** running `grav update` in your projects: a
package may use language features that only a newer compiler has.

A grav built by hand (`build/gravc`) or copied into a Docker image has no
install record; `grav self-update` says so. Rebuild those instead.

## Uninstalling

```bash
./uninstall.sh
```

It removes `PREFIX/bin/grav`, `PREFIX/share/grav` (with the source checkout,
if `install.sh` cloned it; your own checkout stays) and the lines added to
your shell startup file. It asks before deleting the package cache
(`--purge` deletes it, `--yes` asks nothing and keeps it). Packages installed
with apt stay, since other programs may use them.

## Windows: install.ps1

Grav builds Linux programs, so on Windows it runs inside WSL. In PowerShell:

```powershell
irm https://raw.githubusercontent.com/venomous-maker/Grav/main/install.ps1 | iex
```

or `.\install.ps1` from a checkout. The script:

1. checks Windows (WSL 2 needs Windows 10 version 2004, build 19041, or
   Windows 11);
2. installs WSL with Ubuntu if it is missing. That needs an administrator
   PowerShell and usually a restart: open Ubuntu once to create your Linux user,
   then run the script again as yourself;
3. picks a Debian or Ubuntu distribution (asks, defaulting to your default one)
   and runs `install.sh` inside it, with the same questions as on Linux;
4. puts `grav.cmd` in `%LOCALAPPDATA%\Programs\grav\bin` and adds that folder
   to your PATH, so `grav` works in PowerShell and cmd. It runs grav in WSL from
   the current folder and turns Windows paths in the arguments
   (`C:\work\app.grav`) into WSL paths.

Options: `-Yes`, `-Distro <name>`, `-Prefix <dir in WSL>`, `-Optional`,
`-Check`.

```powershell
grav run hello.grav       # builds and runs in WSL
grav self-update          # updates the grav inside WSL
.\uninstall.ps1           # removes grav.cmd, the PATH entry, and grav in WSL
```

Programs grav builds are Linux programs: run them with `grav run`, or
`wsl ./app`. `uninstall.ps1 -KeepWsl` removes only the Windows side; WSL and the
distribution always stay (`wsl --unregister <name>` removes a distribution).

## Docker

```bash
docker build -f docker/Dockerfile -t grav:debian .
docker build -f docker/Dockerfile -t grav:ubuntu --build-arg BASE=ubuntu:24.04 .
```

The image's entry point is `grav`; mount your project at `/app`:

```bash
docker run --rm -v "$PWD":/app grav:debian run hello.grav
docker run --rm -v "$PWD":/app grav:debian build main.grav
```

It holds the compiler, the standard library, a C compiler and the optional
libraries, but not the compiler's own build tools. To ship a program, build it
in the Grav image and copy only the binary into a small one:

```dockerfile
FROM grav:debian AS build
COPY . /app
RUN grav build main.grav -o /app/server --release

FROM debian:bookworm-slim
COPY --from=build /app/server /usr/local/bin/server
CMD ["server"]
```

A built Grav program needs only the C library (plus OpenSSL, zlib or a
database client if it uses them), not grav.

## By hand

```bash
cmake -S . -B build
cmake --build build -j
cmake --install build --prefix ~/.local
```

That installs `~/.local/bin/grav` and `~/.local/share/grav/lib`, without the
install record (so no `grav self-update`).
