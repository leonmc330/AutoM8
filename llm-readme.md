# AutoM8 — LLM reference

Compact reference for tools/agents. Human docs: README.md.

## What
Linux + Windows automation from blocks. A *document* (JSON) holds *buttons*; each button has a *sequence* of *blocks*. Pressing a button runs its blocks in order.
- `autom8-editor [--sequence] [FILE]`: GUI editor. File menu: New Ctrl+N, Open Ctrl+O, Reload from disk Ctrl+R/F5, Save Ctrl+S (Untitled → Save as dialog, then remembers path), Save as Ctrl+Shift+S, Reset to default buttons, Quit Ctrl+Q. Asks Save/Don't save/Cancel before dropping unsaved changes (New/Open/Quit/window close) and confirms Reload. Title shows `name*` when modified. Native dialogs: kdialog/zenity (Linux), common dialog (Windows). Saves are atomic. Interface menu: UI scale 1x–3x (1x = 1920x1080, 2x = 4K), Fit the screen Ctrl+0, Bigger Ctrl+=, Smaller Ctrl+- (shortcuts also in the runner). No FILE given: reopens the last opened file.
- `autom8 [--sequence] [FILE]`: GUI runner (one button per sequence, status line, program list with running dot / exit code / log / kill). Reloads the file when it changes, only while idle.
- Pressing a button while one runs cancels the running sequence; programs already started keep running.

## CLI (runner, no window)
```
autom8 [--sequence FILE] --list                        # button names, one per line
autom8 [--sequence FILE] --press NAME [--verbose] [--stay]
autom8 --version | --help
```
- `--verbose`: also print each program's output as `  [name] line`. `--stay`: after the sequence, keep printing until Ctrl+C.
- `--list` + `--press` together = error. `--verbose/--stay` need `--press`. `--run` = old alias of `--press`.
- stdout: status lines (`NAME: running`, `Running PROG`, messages, `NAME: done`). "user answers Yes" asks `QUESTION [y/N] ` on stdin (only `y`/`Y` = yes; EOF = no).
- Exit: `0` ok, `1` Throw error or unreadable/invalid file, `2` bad args / no such button / no such file, `130` Ctrl+C.
- Terminal modes never create the file; GUI modes create it with an example.

## File location (no FILE given)
1. `sequences.json` next to the executables (portable), else
2. Linux `$XDG_CONFIG_HOME/autom8/sequences.json` (default `~/.config/autom8/`); Windows `%APPDATA%\autom8\sequences.json`.
FILE may be relative; `~`, `$VAR`, `${VAR}` expanded. The editor without FILE first tries `last_file` from the config.

## Config
`config.json` in the same folder (`~/.config/autom8/`, `%APPDATA%\autom8\`): `{"version": 1, "ui_scale": 1.5, "last_file": "/abs/path.json"}`. Missing `ui_scale` → picked from the screen size (min(w/1920, h/1080) rounded to 0.25, 1–3) and saved. Each program rewrites only the key it changes. A config with a newer `version` is read but not written.

## JSON format
```json
{"version": 1, "buttons": [
  {"name": "Start", "color": [r,g,b,a], "sequence": [BLOCK, ...]}
]}
```
`version`: format version (missing = 1). Programs read versions kMinDocumentVersion..kDocumentVersion (`src/common/blocks.hpp`, now 1..1) and refuse others (runner: exit 1 / error; editor: opens Untitled instead, never saves over it).
`color` floats 0–1 (alpha optional). Legacy `{"start":[...],"stop":[...]}` is read as two buttons.

### Blocks (`type` → fields, defaults)
| type | fields |
|---|---|
| `run` | `name` (id used by other blocks), `command`, `shell` false (`sh -c` / `cmd /c`), `cwd` "", `env` "" (`K=V` per line), `match` "" (comma-separated substrings of a process command line: finds/kills it even if not started by autom8), `blocking` false, `min_seconds` 0, `max_seconds` -1, `skip_if_running` true |
| `wait` | `seconds` 1 |
| `wait_until` | `condition`, `timeout` 1 (-1 forever), `stop_on_timeout` false |
| `if` | `condition`, `then` [..], `else` [..] |
| `repeat` | `count` 3 (-1 forever), `body` [..] |
| `repeat_until` | `condition`, `body` [..] — condition checked first; body runs while false |
| `kill` | `name` (run block name; also kills processes matching its `match`), `graceful_seconds` 0 |
| `kill_matching` | `match` (comma-separated substrings, any matches), `graceful_seconds` 0 |
| `kill_all` | `graceful_seconds` 0 — every program named by a run block |
| `message` | `text`, `popup` false |
| `stop` | — (ends sequence quietly) |
| `throw` | `text` (ends sequence as error) |

Semantics:
- `run` non-blocking → next block immediately. Blocking → next when (exited AND ≥ min_seconds) OR ≥ max_seconds (program keeps running).
- `skip_if_running` → does not start a second copy.
- Kill with `graceful_seconds` 0 = hard kill now. >0 = ask to quit (Linux SIGTERM to process group; Windows close its windows), hard kill after N s if still running. Linux kills process group; Windows kills job (all children).

### Condition object
`{"type": T, "not": false, "program": "", "text": "", "number": 0}`
| T | uses | true when |
|---|---|---|
| `output_matches` | program, text=PCRE2 regex | program's output since its last start matches |
| `program_running` | program | started by autom8 and alive, or a process matches its run block `match` |
| `process_running` | text=comma patterns | any process command line contains any pattern |
| `file_exists` | text=path | path exists |
| `command_succeeds` | text=shell command | exit code 0 |
| `exit_code_is` | program, number | program's last exit code == number |
| `user_says_yes` | text=question | user answers Yes (GUI popup / terminal y) |
`not: true` negates.

## Example (examples/web-server.json, Linux)
```json
{"buttons":[
 {"name":"Start","color":[0.2,0.55,0.25,1],"sequence":[
  {"type":"run","name":"Make demo page","command":"mkdir -p /tmp/autom8-demo && echo '<h1>Hello</h1>' > /tmp/autom8-demo/index.html","shell":true,"blocking":true,"max_seconds":10},
  {"type":"run","name":"Web server","command":"python3 -m http.server 8000 --bind 127.0.0.1","cwd":"/tmp/autom8-demo","env":"PYTHONUNBUFFERED=1","match":"http.server 8000","skip_if_running":true},
  {"type":"wait_until","condition":{"type":"command_succeeds","text":"curl -fs http://127.0.0.1:8000 > /dev/null"},"timeout":15,"stop_on_timeout":true},
  {"type":"message","text":"Server running on http://127.0.0.1:8000"},
  {"type":"if","condition":{"type":"user_says_yes","text":"Open it in the browser?"},
   "then":[{"type":"run","name":"Browser","command":"xdg-open http://127.0.0.1:8000"}],"else":[]}]},
 {"name":"Status","sequence":[
  {"type":"if","condition":{"type":"program_running","program":"Web server"},
   "then":[{"type":"message","text":"The web server is running.","popup":true}],
   "else":[{"type":"message","text":"The web server is stopped.","popup":true}]}]},
 {"name":"Stop","sequence":[
  {"type":"kill","name":"Web server","graceful_seconds":3},
  {"type":"message","text":"Server stopped."}]}
]}
```
Missing fields take defaults, so minimal blocks like above are valid. Usage: `autom8 --sequence examples/web-server.json --press Start`.

## Platform notes
- Commands are platform-specific (Linux `sh`, Windows `cmd`); a sequence is not portable as-is.
- Process patterns: Linux `/proc/*/cmdline`; Windows all process command lines.
- Releases: GitHub Releases, betas `v001`, `v002`…, `autom8-vNNN-{linux,windows}-{x86_64,arm64}`. Linux needs glibc ≥ 2.35 + X11/Wayland.

## Build
Deps: CMake ≥ 3.16, C++17, SDL2 + PCRE2 dev (Linux). ImGui + nlohmann/json vendored.
```
cmake -B build -G Ninja && cmake --build build && ctest --test-dir build
```
Options: `-DAUTOM8_VENDORED_DEPS=ON` (static SDL2/PCRE2; default on Windows), `-DAUTOM8_SANITIZE=ON`, `-DAUTOM8_GUI=OFF` (engine + tests only). Windows cross-build: `-DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DMINGW_ARCH=x86_64|aarch64` with `LLVM_MINGW` set.

## Source map
`src/common/blocks.*` model + JSON · `src/common/gui.*` SDL2/ImGui shell · `src/common/util.*` paths/expansion · `src/editor/*` editor · `src/runner/runner.*` engine · `src/runner/process_{posix,win}.cpp` processes · `src/runner/regex.*` PCRE2 · `src/runner/window.*` runner UI · `tests/tests.cpp`.
