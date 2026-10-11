# Safe distro definition updates

Linux can add an unknown distro to the runtime `data/distros.def`. [Issue #28](https://github.com/cupidthecat/cupidfetch/issues/28) covers the former in-place rewrite, unchecked allocations and ignored write errors.

The writer now prepares a bounded replacement, writes a temporary sibling and checks allocation, read, write, flush, sync and close results before publishing it. Existing content bytes and the insertion marker order are preserved. Files and resulting output are limited to 1 MiB. Quotes, backslashes, control characters and oversized inserted fields are rejected.

For an existing ordinary file, replacement preserves its owner, group and permission bits. Symlinks, hardlinks and nonregular files are refused. On Linux, files with extended attributes or ACLs are also refused, as are temporary replacements that inherit attributes; those files remain unchanged instead of losing metadata. A filesystem reporting no extended-attribute support can use the ordinary owner/group/mode path.

The final definitions pathname is not resolved through a symlink before writing. Overlong constructed paths are rejected instead of truncated. A missing destination is published without overwriting a path created concurrently.

This provides atomic replacement, not a guarantee against power loss or uncoordinated concurrent edits. The parent directory is assumed trusted. The source identity and metadata are rechecked before replacement, but other writers are not locked out. The file is synced; the parent directory is not. Cleanup failures can leave a temporary sibling without replacing the original.

Windows has no auto-add path in the existing application. The helper therefore returns without writing on Windows rather than introducing new platform behavior.

## Tests

`make test-distro-file` runs isolated fixtures and is included in `make test`. It covers insertion order, duplicate handling, CRLF/content preservation, long records, missing and empty files, invalid fields, file-size bounds, metadata, unsupported file types and temporary-file cleanup.

Compile-only fault injection tests allocation, read, partial-write, flush, file-sync, close and replacement failures. It is enabled only in the standalone fixture binary with `CF_DISTRO_FILE_TESTING`; application builds have no runtime fault switch. A user-attribute fixture checks that the original inode, content and attribute survive refusal. Filesystems that forbid or do not support user attributes report that fixture as skipped.

The Linux release and AddressSanitizer/UndefinedBehaviorSanitizer aggregate checks passed locally, including the real application's first-time distro JSON test. LeakSanitizer was disabled for the ptrace-based environment. Hosted CI must still be checked for the published revision.
