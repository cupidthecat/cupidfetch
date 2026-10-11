#ifndef CF_SUBPROCESS_H
#define CF_SUBPROCESS_H

#include <stddef.h>
#include <stdio.h>

#define CF_COMMAND_TIMEOUT_MS 3000u
#define CF_COMMAND_OUTPUT_LIMIT (16u * 1024u * 1024u)

/* Run a trusted shell command noninteractively. Return a rewound temporary
 * stream only after stdout reaches EOF and the command exits successfully.
 * Timeout, excess output, nonzero exit and infrastructure errors return NULL,
 * discarding partial output. The caller owns the stream and uses fclose().
 * stdout is captured as bytes; stderr and stdin are connected to the null
 * device. Owned descendants are terminated when capture ends. */
FILE *cf_capture_command(const char *command, unsigned timeout_ms,
                         size_t output_limit);

#endif
