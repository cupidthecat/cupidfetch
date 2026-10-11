#ifndef CF_WINDOWS_PACKAGES_H
#define CF_WINDOWS_PACKAGES_H
#include <stdbool.h>
#include <stdio.h>
/* Pure parsers: caller owns stream; output is unchanged on any failure.
 * At most 1 MiB input, JSON nesting at most 32. Successful parsing does not
 * establish command success: the caller must also check the process status. */
/* Chocolatey: numeric 2-4 component version with optional prerelease suffix;
 * list records require an ASCII ID and digit-leading version, with no noise. */
bool cf_parse_choco_major(FILE *stream, unsigned long *major_out);
/* Trusted static commands; no shell redirection or consent flags. */
const char *cf_choco_list_command(unsigned long major);
bool cf_count_choco_packages(FILE *stream, unsigned long *count_out);
/* Scoop: root apps array; direct app objects require string Name and Version.
 * Empty versions and the exact comma-delimited Info marker "Install failed"
 * are excluded. Duplicate apps/Name/Version/Info keys and escaped NUL reject.
 * Metadata must be valid JSON; names may contain valid UTF-8 or escapes. */
bool cf_count_scoop_export(FILE *stream, unsigned long *count_out);
/* English ASCII table only: Name Id Version [Available] [Source], fixed
 * column positions and all-hyphen separator. No banners, progress, translated
 * headers, Unicode, footers, empty/header-only tables, or control sequences.
 * These unsupported layouts reject instead of returning a guessed count. */
bool cf_count_winget_list(FILE *stream, unsigned long *count_out);
#endif
