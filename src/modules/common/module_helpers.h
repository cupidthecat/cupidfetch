#ifndef MODULE_HELPERS_H
#define MODULE_HELPERS_H

#include "../../cupidfetch.h"

struct process_match {
    const char *proc_name;
    const char *label;
};

void cf_trim_newline(char *str);
bool cf_contains_icase(const char *haystack, const char *needle);
const char *cf_detect_process_label(const struct process_match *candidates, size_t num_candidates);
const char *cf_basename_or_self(const char *path);
bool cf_read_first_line(const char *path, char *buffer, size_t size);
bool cf_read_ulong_file(const char *path, unsigned long *value);
bool cf_starts_with(const char *str, const char *prefix);
char *cf_trim_spaces(char *str);
bool cf_executable_in_path(const char *name);
bool cf_run_command_first_line(const char *command, char *out, size_t out_size);
bool cf_detect_cpu_usage_percent(double *usage_out);
bool cf_build_power_supply_path(char *dest, size_t dest_size, const char *entry_name, const char *suffix);
void cf_format_duration_compact(unsigned long seconds, char *buffer, size_t size);
bool cf_is_drm_card_device(const char *name);
bool cf_build_path3(char *dest, size_t dest_size, const char *prefix, const char *middle, const char *suffix);
const char *cf_gpu_vendor_name(const char *vendor_id);
void cf_append_csv_item(char *dest, size_t dest_size, const char *item);
bool cf_detect_gpu_from_lspci(char *gpu_out, size_t gpu_out_size);
bool cf_read_pci_slot_from_uevent(const char *drm_name, char *slot_out, size_t slot_out_size);
bool cf_detect_gpu_from_pci_slot(const char *pci_slot, char *gpu_out, size_t gpu_out_size);
bool cf_detect_primary_ip(char *iface_out, size_t iface_out_size, char *ip_out, size_t ip_out_size, bool *is_up);
bool cf_get_public_ip(char *ip_out, size_t ip_out_size);
void cf_mask_public_ip(const char *ip_in, char *masked_out, size_t masked_out_size);
bool cf_should_skip_storage_mount(const char *device, const char *mnt_point, const char *fs_type);

#endif
