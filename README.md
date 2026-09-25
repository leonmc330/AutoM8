# AutoM8

Dumb little automations for Linux and Windows, built from blocks.

[![build](../../actions/workflows/build.yml/badge.svg)](../../actions/workflows/build.yml)

Make a **button**, put **blocks** under it (run a program, wait, if/else, repeat,
kill, ask a question...), and press it. AutoM8 was written to start and stop a
whole VR setup with one click, but it works for anything you'd otherwise do by
hand in five terminals.

- `autom8-editor`: a small window to build the buttons and their block sequences
  (drag & drop, saved as you type)
- `autom8`: a window with one big button per sequence, a live status line,
  every program's state and log. It can also run a button from the terminal
  with no window.

The sequences are stored in a plain JSON file, so you can also edit them by hand or
keep them in git.

## Blocks

| Block | What it does |
|---|---|
| **Run** | Starts a program (directly, or through `sh -c`), captures its output. Optional: blocking with min / max time, working dir, extra env vars, skip if already running |
| **Wait seconds** | Waits |
| **Wait until** | Waits for a condition, with an optional timeout (and stop the sequence on timeout) |
| **If ... else** | Runs one of two block lists depending on a condition |
| **Repeat N times** / **Repeat until** | Loops (`-1` = forever) |
| **Kill program** / **Kill matching processes** / **Kill all programs** | SIGKILL, or SIGTERM first with a grace period |
| **Show message** | In the status line, or as a popup |
| **Stop sequence** / **Throw error** | End here, quietly or as an error |

Conditions: the output of a program matches a regex, a program is running, a process
matching a pattern is running, a file exists, a shell command succeeds, a program's
last exit code is N, or the user answers *Yes* to a question. Any condition can be negated.

Regexes are Perl-compatible ([PCRE2](https://www.pcre.org/current/doc/html/pcre2syntax.html)).
Commands and paths expand `~`, `$VAR` and `${VAR}`.

## Download

Ready-made builds are on the [Releases](../../releases) page. Every code change that
passes the tests on all four platforms is published there as a beta: **v001**, **v002**, ...
(`autom8 --version` and the window titles show which one you have).

| | x86_64 (Intel / AMD) | arm64 |
|---|---|---|
| **Linux** | `autom8-vNNN-linux-x86_64.tar.gz` | `autom8-vNNN-linux-arm64.tar.gz` (Raspberry Pi 4/5, ARM laptops...) |
| **Windows 10 / 11** | `autom8-vNNN-windows-x86_64.zip` | `autom8-vNNN-windows-arm64.zip` (Snapdragon laptops...) |

Nothing to install: unpack and run. The Linux builds need glibc 2.35+ (Ubuntu 22.04,
Debian 12, Fedora 36 or newer) and an X11 or Wayland desktop.

## Build

You need CMake 3.16+, a C++17 compiler, and on Linux the SDL2 and PCRE2 development
packages. Dear ImGui and nlohmann/json are included in `third_party/`.

```sh
# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build pkg-config libsdl2-dev libpcre2-dev
# Fedora
sudo dnf install gcc-c++ cmake ninja-build pkgconf SDL2-devel pcre2-devel
# Arch
sudo pacman -S base-devel cmake ninja sdl2 pcre2

cmake -B build -G Ninja
cmake --build build          # -> build/autom8 and build/autom8-editor
ctest --test-dir build       # unit tests
sudo cmake --install build   # optional, to /usr/local/bin
```

Options (`-D...=ON`):

- `AUTOM8_VENDORED_DEPS`: download SDL2 and PCRE2 and link them in statically (how the
  release builds are made; on Linux it then needs the X11 / Wayland headers instead of
  `libsdl2-dev`). On by default on Windows.
- `AUTOM8_SANITIZE`: AddressSanitizer + UBSan, for debugging.
- `AUTOM8_GUI=OFF`: only the sequence engine and its tests, no SDL2 needed (for quick test builds).

### Windows

**From Linux** (how the release builds are made), with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases):

```sh
export LLVM_MINGW=/path/to/llvm-mingw   # the unpacked release folder
cmake -B build-win-x64   -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DMINGW_ARCH=x86_64
cmake -B build-win-arm64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DMINGW_ARCH=aarch64
cmake --build build-win-x64 && cmake --build build-win-arm64
```

**On Windows**, in an [MSYS2](https://www.msys2.org/) CLANG64 (x86_64) or CLANGARM64 (arm64) shell:

```sh
pacman -S --needed $MINGW_PACKAGE_PREFIX-{clang,cmake,ninja}
cmake -B build -G Ninja && cmake --build build
```

SDL2 and PCRE2 are downloaded during the CMake step. The `.exe` files don't need any DLLs
next to them. Visual Studio (MSVC) is supported by the CMake files, but CI doesn't test it.

## Use

```sh
./autom8-editor               # build your buttons
./autom8                      # press them
./autom8 --run Start          # run the "Start" button in the terminal, no window
./autom8 --run Start --verbose --stay   # also print every program's output, keep going until Ctrl+C
./autom8 my-other-file.json   # both programs take another sequence file
./autom8 --version            # which release this is (local builds say "dev")
```

The default file is `sequences.json` **next to the programs** if one is there (handy
for a portable folder), else `~/.config/autom8/sequences.json` on Linux and
`%APPDATA%\autom8\sequences.json` on Windows. It is created with a
small example on first run. The runner reloads it by itself when you save in the editor.

See [`examples/web-server.json`](examples/web-server.json) for a small complete setup
(Linux, needs `python3` and `curl`): **Start** makes a demo page, starts a local web
server, waits until it answers, and asks whether to open it in the browser; **Status**
tells you if it is running; **Stop** asks it to quit and kills it after 3 s.

```sh
./autom8 examples/web-server.json
```

## Linux vs Windows

Sequences work the same on both, with these differences:

| | Linux | Windows |
|---|---|---|
| *shell* option of Run | `sh -c` | `cmd /c` |
| "Ask to quit" (graceful kill) | SIGTERM to the program's process group | closes the program's windows (a program without windows can only be killed hard) |
| Killing a program | kills its process group | kills its job (everything it started) |
| Process patterns search | `/proc/*/cmdline` | every process's command line |

A sequence that runs Linux commands won't work on Windows as-is, and the other way around.

## Be careful with

- **A sequence file can run any command.** Only use files you trust, like a shell script.
- **"Match" patterns are plain text searched in every process's command line.**
  `Kill matching` with the pattern `VRCX` also kills `vim VRCX-notes.txt`. Use long,
  specific patterns, like a full path or `/.mount_VRCX`.
- Programs are started in their own session / job, so they keep running when AutoM8
  exits, unless a Kill block stops them.
- When a program started by a Run block exits, whatever it left running in the
  background (in its process group / job) is stopped too. To start something that
  should outlive it, give it its own Run block.

## Project layout

```
src/common/   document model + JSON (blocks.*), SDL/ImGui window (gui.*), helpers (util.*)
src/runner/   autom8: sequence engine (runner.*), regex, window,
              processes: process.cpp (shared), process_posix.cpp, process_win.cpp
src/editor/   autom8-editor: editor window, block moves (moves.*)
tests/        unit tests (ctest)
examples/     sample sequence files
res/          Windows manifest + version info
cmake/        llvm-mingw toolchain for Windows builds from Linux
```

## License

Free to use, modify and share, for anything. **If you use it in a paid product,**
you must credit it prominently, in bold: **Built with AutoM8 by Leon (leonminnecurt330)**.
See [LICENSE](LICENSE) for the exact terms.
