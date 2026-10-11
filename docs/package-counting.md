# Package-counting checks

This repair addresses the Linux database and command-output parts of [issue #25](https://github.com/cupidthecat/cupidfetch/issues/25).

Package command counts require a successful exit status and complete, error-free output. Counts use nonblank logical records, including a final record without a newline. A record larger than 1 MiB, an embedded NUL or an allocation failure rejects the result without replacing the caller's count.

The native dpkg parser and dpkg-query fallback check the current state independently of the desired action. Held packages and packages selected for removal that are still installed count; residual configuration entries do not. Pacman counts package directories and ignores its metadata files. Slackware continues to count regular package-record files. Directory symlinks are excluded on Linux.

Snap output is requested under `LC_ALL=C`. Its six-column header is validated and omitted from the count. Unknown output shapes fail instead of producing a guessed total.

`make test-packages` runs owned temporary fixtures for logical lines, long records, installed states, Snap output, directory types, missing files and failed child commands. On glibc it also injects a read failure into a fixture stream. No package manager is installed or executed by this suite, and it does not read the host package database. The target is included in `make test`.

The Windows detector uses dedicated Chocolatey, Scoop and WinGet parsers. See [Windows package counts](windows-package-counting.md) for supported formats, command selection and deliberate omissions.

Sources:

- [dpkg-query fields and status values](https://manpages.debian.org/unstable/dpkg/dpkg-query.1.en.html)
- [Snap list columns](https://snapcraft.io/docs/explanation/how-snaps-work/revisions/)
