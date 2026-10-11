# Windows PATH validation

The `Windows build and PATH tests` workflow builds the application and a native
C regression test on `windows-latest`. It finds GCC with PowerShell's
`Get-Command`, prints the compiler version, and fails if compilation or execution
fails. The application smoke tests run `cupidfetch.exe --help` and `--json`.
The JSON run uses a temporary `XDG_CONFIG_HOME` with only the username and shell
modules enabled, then parses and checks the output. It tests USER and USERNAME
fallback values containing literal percent-format sequences. It never probes
installed package managers or makes network requests.

The workflow also runs the portable package-record parser fixtures inherited
from the package-counting repair. These do not validate real Windows package-manager
table formats.

The PATH fixture binary creates its own temporary directory, supplies its own PATH
and PATHEXT, and removes its files afterward. It does not install or invoke
package managers. Empty files represent commands because the helper checks
command availability rather than launching commands or validating their format.

The fixtures cover:

- `.exe`, `.com`, `.bat`, and `.cmd` discovery, including `scoop.cmd` in a directory
  containing spaces.
- Explicit suffixes, case-insensitive suffixes, and dotted command names.
- Missing files, unsupported suffixes, extensionless files, and directories.
- PATH and PATHEXT changes after positive and negative cached answers.
- Quoted PATH entries, multiple directories and suffixes, and empty entries.
- Unset or empty PATHEXT, which uses `.COM;.EXE;.BAT;.CMD`.
- PATH and PATHEXT values longer than the cache buffers.

Windows uses `_access(path, 0)` followed by a regular-file check. Microsoft's
[CRT documentation](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/access-waccess?view=msvc-170)
lists only modes 0, 2, 4, and 6. Mode 1 can invoke the invalid-parameter handler.
POSIX builds retain the `access(path, X_OK)` permission check and also reject
directories. `make test-path` checks executable permissions, directory rejection,
PATH cache changes and long PATH values on Linux. It is included in `make test`.

To run the native fixtures locally from the repository root in PowerShell with
MinGW GCC on PATH:

```powershell
$gcc = (Get-Command gcc -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
New-Item -ItemType Directory -Force tests/bin | Out-Null
& $gcc -o tests/bin/test_windows_path.exe tests/test_windows_path.c src/modules/common/module_helpers.c -O2 -Wall -std=c99 -D_WIN32_WINNT=0x0601 -Ilibs -lws2_32
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& ./tests/bin/test_windows_path.exe
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
```

A Linux build or cross-compile does not establish that these fixtures pass on
Windows. Check the native workflow result for the revision under review.
