# Week 9 and 10 requirement mapping

The supplied course documents are used as project specifications. Their terminal/Git snippets are illustrative, not commands copied blindly into the implementation.

| Course outcome | Implementation | Verification |
| --- | --- | --- |
| Standard input/output/error descriptors | `redirect.c`, pipeline file actions | Input, output and stderr integration tests |
| Overwrite, append, input, error operators | Lexer/parser plus ordered redirects | Individual and combined redirection tests |
| File creation flags and permissions | Read-only, append, truncate; mode 0644 | Real temporary output files |
| Redirection after command parsing | Common pipeline executor | Pipelines combined with redirection |
| Create and join threads | Monitor lifecycle and four-worker demo | `threads`, prompt shutdown test |
| Protect shared state with mutexes | Monitor mutex and counter mutex | Exact counter total of 400000 |
| Understand race conditions | Deterministic lost-update demo using atomic reads/stores | Expected 2000 versus actual 1000 |
| Background monitoring | Joinable thread with timed condition wait | Enable/status/shutdown checks |
| Build pthread support | `-pthread` compilation/link | Linux/macOS CI |

The source documents' detached infinite monitor and partial redirection samples are expanded into a complete implementation. No claim is made that the supplied Week 6–8 screenshot verified this code; those features are implemented and tested here.
