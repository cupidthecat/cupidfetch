# Maintenance and validation

The October 2026 review started from `5c2927e13849493e010cdb70728b131378db4162`.

- [Initial repairs and regression coverage](audit-2026-10-11.md): output, JSON, configuration, installation, builds and hardware reporting.
- [Package-counting checks](package-counting.md): installed states, metadata, complete records and command failures.
- [Windows package counts](windows-package-counting.md): version-aware commands, JSON and supported table layouts.
- [Windows PATH validation](windows-validation.md): native file probes, command suffixes and platform tests.
- [Safe distro definition updates](distro-file-updates.md): bounded preparation, atomic replacement and failure preservation.
- [Detector subprocess limits](subprocess-capture.md): timeouts, output limits and owned process cleanup.
- [Build and test commands](usage.md#tests-and-debugging).

The combined local Linux suite includes parser, config, unit/accounting, package,
PATH, distro-file, subprocess and Windows package-format C tests, five CLI tests and isolated installer tests. Release
and AddressSanitizer/UndefinedBehaviorSanitizer runs passed. LeakSanitizer was
disabled for the ptrace-based review environment. Hosted checks remain the source
of truth for each published commit; local Linux results do not establish a native
Windows pass.

## Validation boundaries

Windows package counts deliberately omit unsupported WinGet layouts. Subprocess
limits apply per command, and distro updates refuse metadata they cannot preserve.
The linked documents describe these boundaries. The review and passing tests are
not an exhaustive proof that every module works on every supported system.
