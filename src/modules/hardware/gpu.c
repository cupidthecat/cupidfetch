#include "../../cupidfetch.h"
#include "../common/module_helpers.h"

static bool read_uevent_value(
    const char *drm_name,
    const char *key,
    char *out,
    size_t out_size
) {
    char path[512];
    if (!cf_build_path3(path, sizeof(path), "/sys/class/drm/", drm_name, "/device/uevent")) {
        return false;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    char key_prefix[64];
    if (snprintf(key_prefix, sizeof(key_prefix), "%s=", key) <= 0) {
        fclose(fp);
        return false;
    }

    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, key_prefix, strlen(key_prefix)) != 0) continue;

        char *value = line + strlen(key_prefix);
        cf_trim_newline(value);
        strncpy(out, value, out_size);
        out[out_size - 1] = '\0';
        found = out[0] != '\0';
        break;
    }

    fclose(fp);
    return found;
}

static bool read_first_pci_slot_from_sys(char *slot_out, size_t slot_out_size) {
    if (!slot_out || slot_out_size == 0) return false;

    FILE *fp = popen("grep -Rsm1 '^PCI_SLOT_NAME=' /sys 2>/dev/null", "r");
    if (!fp) return false;

    char line[512] = "";
    bool ok = fgets(line, sizeof(line), fp) != NULL;
    pclose(fp);
    if (!ok) return false;

    char *slot = strstr(line, "PCI_SLOT_NAME=");
    if (!slot) return false;

    slot += strlen("PCI_SLOT_NAME=");
    cf_trim_newline(slot);
    strncpy(slot_out, slot, slot_out_size);
    slot_out[slot_out_size - 1] = '\0';
    return slot_out[0] != '\0';
}

static bool is_microsoft_kernel(void) {
    char kernel_release[256] = "";
    if (!cf_read_first_line("/proc/sys/kernel/osrelease", kernel_release, sizeof(kernel_release))) {
        return false;
    }
    return cf_contains_icase(kernel_release, "microsoft");
}

void get_gpu() {
    DIR *dir = opendir("/sys/class/drm");
    char gpu_summary[256] = "";

    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (!cf_is_drm_card_device(entry->d_name)) continue;

            char lspci_desc[256] = "";
            char pci_slot[64] = "";
            if (cf_read_pci_slot_from_uevent(entry->d_name, pci_slot, sizeof(pci_slot)) &&
                cf_detect_gpu_from_pci_slot(pci_slot, lspci_desc, sizeof(lspci_desc))) {
                cf_append_csv_item(gpu_summary, sizeof(gpu_summary), lspci_desc);
                continue;
            }

            char path[512];
            char vendor_id[64] = "";
            char driver_link[512] = "";

            if (cf_build_path3(path, sizeof(path), "/sys/class/drm/", entry->d_name, "/device/vendor")) {
                cf_read_first_line(path, vendor_id, sizeof(vendor_id));
            }

            if (cf_build_path3(path, sizeof(path), "/sys/class/drm/", entry->d_name, "/device/driver")) {
                ssize_t n = readlink(path, driver_link, sizeof(driver_link) - 1);
                if (n > 0) {
                    driver_link[n] = '\0';
                } else {
                    driver_link[0] = '\0';
                }
            }

            const char *vendor = (vendor_id[0] != '\0') ? cf_gpu_vendor_name(vendor_id) : NULL;
            const char *driver = NULL;
            if (driver_link[0] != '\0') {
                driver = strrchr(driver_link, '/');
                driver = driver ? (driver + 1) : driver_link;
            }

            char item[640];
            if (vendor && driver) {
                snprintf(item, sizeof(item), "%s (%.600s)", vendor, driver);
            } else if (vendor) {
                snprintf(item, sizeof(item), "%s", vendor);
            } else if (driver) {
                snprintf(item, sizeof(item), "%.600s", driver);
            } else {
                char uevent_driver[128] = "";
                char modalias[256] = "";

                if (read_uevent_value(entry->d_name, "DRIVER", uevent_driver, sizeof(uevent_driver))) {
                    snprintf(item, sizeof(item), "%.600s", uevent_driver);
                } else if (read_uevent_value(entry->d_name, "MODALIAS", modalias, sizeof(modalias))) {
                    const char *alias_label = modalias;
                    const char *colon = strrchr(modalias, ':');
                    if (colon && colon[1] != '\0') {
                        alias_label = colon + 1;
                    }

                    if (cf_contains_icase(alias_label, "vgem") || cf_contains_icase(modalias, "platform:vgem")) {
                        continue;
                    }

                    snprintf(item, sizeof(item), "%.600s", alias_label);
                } else {
                    continue;
                }
            }

            cf_append_csv_item(gpu_summary, sizeof(gpu_summary), item);
        }
        closedir(dir);
    }

    if (gpu_summary[0] == '\0') {
        if (!cf_detect_gpu_from_lspci(gpu_summary, sizeof(gpu_summary))) {
            char pci_slot[64] = "";
            if (read_first_pci_slot_from_sys(pci_slot, sizeof(pci_slot))) {
                if (is_microsoft_kernel()) {
                    snprintf(gpu_summary, sizeof(gpu_summary), "%s Microsoft Corporation Basic Render Driver", pci_slot);
                } else {
                    snprintf(gpu_summary, sizeof(gpu_summary), "%s", pci_slot);
                }
            } else {
                return;
            }
        }
    }

    print_info("GPU", "%s", 20, 30, gpu_summary);
}
