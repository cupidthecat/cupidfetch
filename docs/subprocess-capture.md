# Detector subprocess limits

Detector commands run through `cf_capture_command`. Each capture has a three-second
execution deadline and a 16 MiB stdout limit. CPU, GPU, package and common command
helpers use the same limits. A slow or failed command leaves its value unavailable;
partial output is discarded.

The helper returns a rewound temporary stream only after stdout reaches EOF and
the command exits successfully. Callers close that stream with `fclose`. Standard
input and standard error go to the platform null device, so detector commands
cannot wait for terminal input or mix diagnostics into JSON output.

On POSIX systems, the shell gets its own process group. The parent keeps the shell's
PID reserved until group cleanup, then reaps it. On Windows, the shell starts
suspended, joins a kill-on-close job and then runs with an explicit inherited-handle
list. Cleanup terminates remaining processes in the owned group or job, including
children that keep stdout open after their parent exits.

## Scope

The deadline applies to each command, not an entire refresh. Several unavailable
commands can consume several deadlines. Direct file and directory probes are
unchanged. Kernel scheduling, process termination and temporary-file I/O can delay
cleanup beyond the execution deadline; this is not a hard real-time guarantee.

The commands are trusted detector command strings. This API does not make arbitrary
shell input safe. A program that deliberately escapes a POSIX process group is
outside the cleanup contract. Windows job assignment failure rejects the command
before it starts executing.

## Tests

`make test-subprocess` runs the test executable as its own synthetic child. It
checks successful and empty output, binary bytes, the exact output limit, failed
exits, closed stdin, hangs with and without output, a closed pipe with a live child,
continuous output and a descendant holding the pipe open. Linux checks the exact
descendant PID exits from SIGKILL; Windows checks its retained process handle has
terminated. Repeated captures exercise handle and descriptor cleanup. The Windows workflow builds and runs the same suite.

The fixtures do not call installed package managers or network tools. Full local
release and AddressSanitizer/UndefinedBehaviorSanitizer runs passed during review;
check the hosted runs for the published commit's native Windows result.
