Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
License: MIT (see LICENSE file in repository root)

# Host bootstrap configuration

`MorphicEngine` accepts exactly one argument: the path to a bootstrap
configuration file. A relative path is resolved from the process working
directory. The Visual Studio Host project starts with
`development/bootstrap/flow-test.cfg` from the repository root. The alternate
`development/bootstrap/flow-test-inline.cfg` runs batch work on the requesting
thread. Both presets select the current flow-test Executive explicitly.
The file name is opened through the narrow C runtime API; non-ASCII file names
on Windows therefore depend on the process code page.

The file supplies only settings needed to start the Host and select the
Executive. It does not define application configuration for a future Executive.
The Host reads it before installing its context or starting engine services.
The file is closed and the bounded parsing buffer is gone before runtime
startup. Selected values are copied into fixed-size settings storage; no
runtime pointer refers to the file buffer.

## Format

The file is at most 4096 bytes. It contains one `key=value` setting per line,
with LF or CRLF endings and an optional UTF-8 byte-order mark. Empty lines and
lines beginning with `#` are ignored. Keys are case-sensitive. There is no
whitespace trimming, quoting, escaping, inclusion, or environment expansion.
Values must be non-empty and cannot have leading/trailing spaces or control
bytes. A value may contain `=` after the first separator. Unknown and duplicate
keys fail startup with a line-numbered diagnostic on stderr.

| Key | Meaning | Default |
| --- | --- | --- |
| `executive` | Logical DLL path for the Executive; required | None |
| `log-directory` | Existing log directory, relative to the working directory when not absolute | `development/logical-roots/logs` |
| `log-tag` | Optional 1–48 character log-name tag using the existing log-tag rules | No tag |
| `host-workers` | Positive decimal request for I/O and conditioning workers | `2` |
| `batch-runners` | Nonnegative decimal request for batch runners | `8` |

String values must fit in 511 bytes, except `log-tag`, which retains its
48-character limit. Decimal counts must fit in `uint32_t`. The Host still
applies its hardware and platform limits after parsing. The loader does not
attempt to resolve the Executive or open the log directory; the existing Host
startup paths report those failures.

For example:

```text
# Flow-test Executive
executive=package:/bin/MorphicExecutive.dll
log-directory=development/logical-roots/logs
host-workers=2
batch-runners=8
```

Launch it from the repository root with:

```powershell
.\build\bin\x64\Debug\MorphicEngine.exe development/bootstrap/flow-test.cfg
```

Host acceptance and module lifecycle harnesses write temporary configuration
files for case-specific Executive paths, log tags and worker counts. They pass
one file path to the process and remove each file after the run. This keeps the
launch interface the same for development presets and tests.
