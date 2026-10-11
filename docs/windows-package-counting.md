# Windows package counts

Package counts use a parser for each supported command format. A command must exit
successfully within the [capture limits](subprocess-capture.md); malformed or
unsupported output omits that manager's count. Parser inputs are limited to 1 MiB.

## Chocolatey

The detector reads `choco --version` first. Versions before 2 use
`choco list --local-only --limit-output --no-color`; version 2 and later use
`choco list --limit-output --no-color`. This avoids the removed local-only option
on newer clients and avoids a remote package query on older ones.

Each output record must contain an ASCII package ID, a pipe and a nonempty version
starting with a digit. Banners, headers and unexpected extra fields reject the
result. Nonzero exit codes, including optional enhanced no-results codes, omit the
count. No package is installed and no license is accepted.

## Scoop

The detector uses `scoop export` and parses the JSON on stdout. It counts direct
entries in the top-level `apps` array with nonblank string `Name` and `Version`
fields. Entries marked `Install failed` in the comma-separated `Info` field, or
with an empty version, are excluded. User and global entries remain separate.
Buckets and nested metadata do not add to the count.

The parser validates the complete JSON document, UTF-8, escaped characters and
surrogate pairs. Nesting deeper than 32 levels, duplicate critical keys, embedded
NULs, missing fields and incorrect types reject the result. Unsupported older
export formats also omit the count.

## WinGet

The detector uses `winget list --disable-interactivity`. It does not accept source
agreements or select a different source to avoid a prompt. Older clients that do
not support this flag fail without an interactive retry.

WinGet does not provide a documented JSON mode for `list`. The fallback accepts
only English ASCII fixed-column tables headed `Name Id Version`, with optional
`Available` and `Source` columns. It validates the separator and required row
fields before counting. Translated headers, Unicode names, progress/control
sequences, unexpected notices and unsupported layouts omit the entire WinGet
count. This restriction favors a missing count over a guessed number. `winget
export` is not equivalent because it excludes applications without source matches.

## Validation

`make test-windows-packages` runs synthetic parser fixtures on Linux and Windows.
They cover version selection, complete records, malformed input, JSON metadata,
escapes, failed installations, input limits and read failures where supported.

The Windows workflow also runs `tests/test_windows_package_cli.ps1` against the
real application. It replaces PATH with an owned temporary directory containing
fake `.cmd` managers, verifies the exact command arguments and tests both
Chocolatey version paths plus invalid and failed outputs. It does not run package
managers installed on the runner.

## Sources

- [Chocolatey migration guide](https://docs.chocolatey.org/en-us/guides/upgrading-to-chocolatey-v2-v6/)
- [Chocolatey list options](https://docs.chocolatey.org/en-us/choco/commands/list/)
- [Scoop export implementation](https://github.com/ScoopInstaller/Scoop/blob/master/libexec/scoop-export.ps1)
- [Scoop list implementation](https://github.com/ScoopInstaller/Scoop/blob/master/libexec/scoop-list.ps1)
- [WinGet list options](https://learn.microsoft.com/en-us/windows/package-manager/winget/list)
- [WinGet export limitations](https://learn.microsoft.com/en-us/windows/package-manager/winget/export)
