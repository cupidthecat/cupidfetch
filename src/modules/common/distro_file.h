#ifndef CF_DISTRO_FILE_H
#define CF_DISTRO_FILE_H

#include <stdbool.h>

/* Returns true only when an entry was added. Existing entries and failures
 * return false. Files larger than this bound are left untouched. Windows
 * detection does not auto-add entries; on Windows this is a no-write stub.
 * Linux files with extended attributes (including ACLs), or replacements
 * inheriting them, are refused rather than silently changing that metadata.
 * Replacement is atomic, without crash-durability or writer-lock guarantees. */
#define CF_DISTRO_FILE_MAX_BYTES (1024u * 1024u)
bool cf_insert_auto_added_distro(const char *path, const char *id, const char *name);

#ifdef CF_DISTRO_FILE_TESTING
/* Compiled only into the standalone test binary, never controlled by input. */
enum cf_distro_file_fault {
    CF_DISTRO_FAULT_NONE, CF_DISTRO_FAULT_ALLOC, CF_DISTRO_FAULT_TEMP_ALLOC,
    CF_DISTRO_FAULT_READ, CF_DISTRO_FAULT_WRITE, CF_DISTRO_FAULT_FLUSH,
    CF_DISTRO_FAULT_SYNC, CF_DISTRO_FAULT_CLOSE,
    CF_DISTRO_FAULT_REPLACE
};
void cf_distro_file_test_fault(enum cf_distro_file_fault fault);
#endif

#endif
