# ShellForge

A parallel task execution shell written in C for the Operating Systems and Systems Programming project. ShellForge executes real programs, connects pipelines, redirects files, runs background jobs, and demonstrates POSIX threads and synchronization. A browser terminal connects to the same compiled shell through a Python PTY server.

## Quick start

Requirements: Linux or macOS, a C11 compiler, Make, POSIX threads, and Python 3.10+ for the browser terminal and tests. On Windows, use WSL2.

```sh
git clone https://github.com/Hemani29/ShellForge.git
cd ShellForge
make
make run
```

Until the upstream pull request is merged, get the completed implementation with:

```sh
git clone --branch feat/complete-shellforge-weeks-6-10 https://github.com/kalapalashanmukh-2309/ShellForge.git
```

```text
shellforge> echo "Hello, ShellForge"
Hello, ShellForge
shellforge> exit
```

`make run` builds the executable first. `bin/shellforge -c 'command'` executes one command or pipeline. Interactive prompts are suppressed when reading a script or piped input. EOF exits cleanly.

## Browser terminal

```sh
make web
```

Open the full `http://127.0.0.1:8765/#token=...` link printed by the server. It connects automatically. The page includes a terminal, five insertable examples, a quick guide, Ctrl+C, clear output, and session controls. No npm installation or external CDN is required.

This executes **the actual C binary**, not simulated JavaScript commands. Output, pipelines, file operations, terminal resizing, interactive input, and signals travel through a pseudo-terminal. The initial working directory is `web/workspace/`; files remain there between sessions. `cd` persists within a session. The access token changes whenever the server restarts and is removed from the browser URL after loading.

The browser terminal is **single-user**. Local commands have the permissions of the account running the server; the workspace directory is a starting directory, not a filesystem sandbox. The server listens on loopback, checks a random bearer token, validates API host/origin headers, bounds output retention, and closes idle sessions after 30 minutes. Keep the server private. Use the container option below when you want separation from host files.

### Hosted frontend

Website: [ShellForge Terminal](https://shellforge-terminal.mellowsole1.chatgpt.site) (private to the publishing account).

The hosted website is a frontend for your own running server. Static hosting cannot execute `fork`, `exec`, or POSIX threads. In its connection dialog, use the displayed origin-specific startup command, then enter your local server's access token. Browser support for local-network requests varies; the local website link always avoids cross-origin connection requirements.

An always-online, multi-user service requires a separately provisioned Linux backend with HTTPS, identity-based access and per-user isolation. It is not provided by the static deployment.

### Container option

```sh
docker build -t shellforge .
docker run --rm --init --cap-drop=ALL --security-opt=no-new-privileges \
  --pids-limit=128 --memory=256m --cpus=1 \
  -p 127.0.0.1:8765:8765 shellforge
```

The non-root container holds the executable and terminal workspace. Open its printed token link. Files inside the container are removed when it is removed. Do not mount your home directory or Docker socket. Docker is optional and is not needed for `make web`.

## Commands and behavior

| Feature | Example |
| --- | --- |
| External command and arguments | `ls -l` |
| Single/double quotes | `printf "%s\n" "two words"` |
| Escaped space | `echo hello\ world` |
| Three-stage pipeline | `printf "pear\napple\npear\n" \| sort \| uniq` |
| Overwrite output | `echo first > output.txt` |
| Append output | `echo second >> output.txt` |
| Read input | `cat < output.txt` |
| Redirect errors | `ls missing 2> errors.txt` |
| Combine redirection and pipes | `cat < output.txt \| wc -l > count.txt` |
| Start independent concurrent work | `sleep 3 &` |
| List background tasks | `jobs` |
| Wait for all background tasks | `wait` |
| Last exit status | `status` |
| Change/list directory | `cd directory`, `pwd` |
| Monitor heartbeat | `monitor on`, `monitor off`, `monitor status` |
| Mutex demonstration | `threads` |
| Lost-update demonstration | `race-demo` |
| Help / quit | `help`, `exit [0-255]` |

Run `sleep 3 &` twice, then `jobs` and `wait`. The two tasks overlap instead of taking six seconds sequentially. Background stdin defaults to `/dev/null`; explicit input redirection overrides it. Job completion is reaped at command boundaries. `exit` and EOF terminate and reap outstanding jobs rather than leaving them running.

Redirection is applied left-to-right, after pipeline wiring, and operators need not have surrounding spaces. Built-ins run in the main shell when used alone, with original descriptors restored afterward. Built-ins in pipelines or background commands run in fresh child processes, so their state changes do not affect the parent. A pipeline returns the final stage's exit status. Missing programs return 127; invocation errors return 126; invalid syntax returns 2; interrupted foreground programs return 130.

## Week-by-week development

### Weeks 1–5 — Foundation, memory, processes, and IPC

- Interactive read–execute loop, Makefile, and dynamically growing input.
- Process creation, parent/child IDs, waiting and reaping.
- Zombie demonstration and pipe-based producer/consumer communication.
- Fixed `ls -l src | grep '\.c$'` teaching demonstration.
- Earlier examples remain available as `process`, `pipe`, `ls-grep`, and `zombie` commands. The fixed `ls-grep` demo expects the repository root as its working directory; normal pipelines work from any directory.
- The producer's child now exits correctly; captured process-demo output is flushed before `_exit`.

### Week 6 — Signals and command execution

- Parse arguments and execute external commands through `execvp()` in fresh child processes.
- Handle Ctrl+C at the prompt without exiting the shell.
- Place pipelines in their own process groups and give foreground commands the terminal.
- Reset child signal dispositions and restore terminal ownership after completion.
- Ctrl+D exits; interrupted reads recover.

The Week 6–8 implementation is based on the milestone names supplied with the project and the prerequisites needed by Weeks 9–10; their original handbooks were not supplied.

### Week 7 — Pipelines and parallel work

- Support up to 32 pipeline stages, starting every stage before waiting.
- Close unused pipe ends to prevent hangs and ensure EOF propagation.
- Run independent commands and pipelines in the background with `&`.
- Track up to 64 concurrent jobs with `jobs`, `wait`, and exit-time cleanup.
- Support quoted operators and built-ins in pipelines.

### Week 8 — Memory checks and testing

- Provide `make asan` and `make asan-test` with AddressSanitizer and UndefinedBehaviorSanitizer.
- Free parsed arguments, redirection paths, input, and pipeline structures.
- Preserve allocations on `realloc` failure and enforce input/argument limits.
- Include integration tests for EOF, long input, syntax errors, process cleanup, redirection, pipelines, concurrency and terminal signals.
- GitHub Actions builds and tests Linux and macOS; sanitizer execution runs on Linux.

**Local sanitizer limitation:** on the development host (macOS 26.5.1, Apple Clang 17), ASan builds but its runtime hangs before `main()`, including for `--version`. This is not reported as a passing sanitizer run. See the Linux Actions job for the runnable sanitizer check.

### Week 9 — File descriptors and I/O redirection

Implements the supplied Week 9 learning outcomes:

- `>` uses create/write/truncate, `>>` uses create/write/append.
- `<` opens input read-only; `2>` redirects standard error.
- New output files use mode `0644`, subject to the user's umask.
- Built-in redirection uses `open()`, `dup2()`, and `close()` with restoration.
- Child redirection uses POSIX spawn file actions, which open/duplicate descriptors before executing the child.
- Redirection works with ordinary commands, built-ins, multiple streams, and pipelines.
- Missing filenames and invalid syntax are rejected without crashing.

The handbook's sample implements only `>` and `>>`; this project also implements the required `<` and `2>`. Redirection is handled by the common parser/executor, not nested inside the legacy `pipe` command.

### Week 10 — POSIX threads and concurrency

- Start one background monitoring thread with `pthread_create()`.
- Protect its shared counters and enable/stop flags using a mutex.
- Use a condition variable for the ten-second heartbeat and immediate shutdown.
- Enable heartbeat output with `monitor on`; it is off initially to keep the terminal quiet.
- Stop and `pthread_join()` the monitor before exiting, avoiding a detached infinite thread.
- `threads` creates and joins four workers, protecting 400,000 total counter updates with a mutex.
- `race-demo` makes two workers read before either writes, producing 1,000 instead of 2,000 updates. Separate atomic loads/stores make this a defined lost-update demonstration, rather than introducing undefined C data races.

External execution uses `posix_spawn()`-family calls plus a fresh `execvp()` launcher. This avoids running built-ins or library code in the forked memory of a multithreaded parent. The earlier standalone examples still teach `fork()` and `wait()`.

## Build and test

[Implementation pull request](https://github.com/Hemani29/ShellForge/pull/1) · [Verified Linux/macOS run](https://github.com/kalapalashanmukh-2309/ShellForge/actions/runs/37881012086). This run passed 33 shell tests and 7 backend tests on both platforms, plus all 33 shell tests under ASan/UBSan on Linux.

```sh
make test          # Shell integration and PTY signal tests
make test-web      # HTTP auth, real browser-backend execution, cleanup
make asan-test     # ASan + UBSan, best run on Linux
make clean
```

Tests use temporary directories and real binaries. No mocks substitute for command execution. Python's standard library is sufficient. `CC`, `CFLAGS`, and `PYTHON` can be overridden. Header changes trigger rebuilding through generated dependency files.

## Structure

```text
include/              Shared types and module interfaces
src/
  main.c              Interactive/script entry point and child launch modes
  input.c             Bounded dynamic input and EOF handling
  parser.c            Quoting, arguments, pipelines and redirection
  process.c           Spawn, process groups, pipelines and job management
  redirect.c          Descriptor setup and restoration
  builtin.c           Shell built-ins
  signals.c           Prompt and terminal signal handling
  thread.c            Monitor, mutex workers and lost-update demonstration
examples/             Standalone Week 3–5 demonstrations
web/
  server.py           Authenticated local HTTP-to-PTY bridge
  pty_child.py        Single-threaded controlling-terminal launcher
  static/             Browser app and vendored xterm.js assets
  workspace/          Local session files (ignored by Git)
tests/                Integration tests
.github/workflows/    Linux/macOS CI
```

## Scope and limits

This is an educational shell, not a Bash replacement. It does not implement variable/glob/tilde expansion, command substitution, heredocs, descriptor duplication (`2>&1`), `&&`, `||`, `;`, history editing, or `fg`/`bg` suspended-job control. Stopped foreground jobs are terminated with a diagnostic. Comments begin with `#` at a token boundary. Unquoted unsupported separators are rejected; expansion syntax remains literal.

Limits: 65,536 input bytes, 256 arguments per command, 32 stages, 32 redirections per command, 64 concurrent background jobs. The browser bridge is a private single-user tool, not a public shell service.

## Credits and third-party code

ShellForge began as Hemani29's OSSP course project. Week 9 and 10 requirements come from the supplied ShellForge course handbooks. The terminal renderer uses xterm.js 6.0.0 and addon-fit 0.11.0, distributed under MIT; their license files are included in `web/static/vendor/`. No project-wide license has been chosen on behalf of the original author.
