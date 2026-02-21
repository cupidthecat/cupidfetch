// File: print.c
// -----------------------
#include "cupidfetch.h"
#include <locale.h>
#include <wchar.h>

extern int wcwidth(wchar_t wc);

#define MAX_CAPTURE_LINES 256
#define MAX_CAPTURE_LINE_LEN 512
#define MAX_RENDER_LINES 1024

static bool g_capture_info = false;
static char g_info_lines[MAX_CAPTURE_LINES][MAX_CAPTURE_LINE_LEN];
static size_t g_info_line_count = 0;
static char g_info_keys[MAX_CAPTURE_LINES][64];
static char g_info_values[MAX_CAPTURE_LINES][384];
static size_t g_info_kv_count = 0;

static void ensure_utf8_locale(void) {
    static bool initialized = false;
    if (!initialized) {
        setlocale(LC_CTYPE, "");
        initialized = true;
    }
}

static int utf8_codepoint_width(const char *s, size_t max_len, size_t *consumed) {
    if (!s || max_len == 0) {
        if (consumed) *consumed = 0;
        return 0;
    }

    mbstate_t state;
    memset(&state, 0, sizeof(state));

    wchar_t wc;
    size_t read = mbrtowc(&wc, s, max_len, &state);
    if (read == (size_t)-1 || read == (size_t)-2) {
        if (consumed) *consumed = 1;
        return 1;
    }
    if (read == 0) {
        if (consumed) *consumed = 1;
        return 0;
    }

    int width = wcwidth(wc);
    if (width < 0) width = 0;

    if (consumed) *consumed = read;
    return width;
}

static size_t utf8_display_width(const char *s) {
    if (!s) return 0;

    ensure_utf8_locale();

    size_t width = 0;
    size_t len = strlen(s);
    size_t pos = 0;

    while (pos < len) {
        size_t consumed = 0;
        int cp_width = utf8_codepoint_width(s + pos, len - pos, &consumed);
        if (consumed == 0) break;
        width += (size_t)cp_width;
        pos += consumed;
    }

    return width;
}

static size_t utf8_nbytes_for_columns(const char *s, size_t max_columns) {
    if (!s) return 0;

    ensure_utf8_locale();

    size_t len = strlen(s);
    size_t pos = 0;
    size_t used_columns = 0;

    while (pos < len) {
        size_t consumed = 0;
        int cp_width = utf8_codepoint_width(s + pos, len - pos, &consumed);
        if (consumed == 0) break;

        size_t cpw = (size_t)cp_width;
        if (used_columns + cpw > max_columns) break;

        used_columns += cpw;
        pos += consumed;
    }

    return pos;
}

static size_t scale_repeat_for_index(size_t index, size_t num, size_t den) {
    if (den == 0) return 0;

    unsigned long long next = (unsigned long long)(index + 1) * (unsigned long long)num;
    unsigned long long curr = (unsigned long long)index * (unsigned long long)num;
    return (size_t)(next / den - curr / den);
}

static size_t gcd_size_t(size_t a, size_t b) {
    while (b != 0) {
        size_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static void truncate_for_width(const char *text, size_t max_width, char *out, size_t out_size) {
    if (!out || out_size == 0) return;

    if (!text) {
        out[0] = '\0';
        return;
    }

    if (max_width == 0) {
        out[0] = '\0';
        return;
    }

    size_t text_width = utf8_display_width(text);
    if (text_width <= max_width) {
        snprintf(out, out_size, "%s", text);
        return;
    }

    const char *ellipsis = "…";
    size_t ellipsis_width = utf8_display_width(ellipsis);
    size_t content_width = (max_width > ellipsis_width) ? (max_width - ellipsis_width) : 0;
    size_t content_bytes = utf8_nbytes_for_columns(text, content_width);

    if (max_width <= ellipsis_width) {
        size_t ellipsis_bytes = utf8_nbytes_for_columns(ellipsis, max_width);
        if (ellipsis_bytes >= out_size) ellipsis_bytes = out_size - 1;
        memcpy(out, ellipsis, ellipsis_bytes);
        out[ellipsis_bytes] = '\0';
        return;
    }

    if (content_bytes >= out_size) content_bytes = out_size - 1;
    memcpy(out, text, content_bytes);
    out[content_bytes] = '\0';

    size_t remaining = out_size - content_bytes;
    if (remaining > 1) {
        strncat(out, ellipsis, remaining - 1);
    }
}

static size_t utf8_nbytes_for_column_range(const char *s, size_t start_col, size_t max_columns, size_t *out_start_byte) {
    if (!s || max_columns == 0) {
        if (out_start_byte) *out_start_byte = 0;
        return 0;
    }

    ensure_utf8_locale();

    size_t len = strlen(s);
    size_t pos = 0;
    size_t col = 0;
    bool started = false;
    size_t start_byte = 0;
    size_t end_byte = len;
    size_t end_col = start_col + max_columns;

    while (pos < len) {
        size_t consumed = 0;
        int cp_width = utf8_codepoint_width(s + pos, len - pos, &consumed);
        if (consumed == 0) break;

        size_t cpw = (size_t)cp_width;
        size_t next_col = col + cpw;

        if (!started && next_col > start_col) {
            started = true;
            start_byte = pos;
        }

        if (started && col >= end_col) {
            end_byte = pos;
            break;
        }

        pos += consumed;
        col = next_col;
    }

    if (!started) {
        if (out_start_byte) *out_start_byte = len;
        return 0;
    }

    if (out_start_byte) *out_start_byte = start_byte;
    return end_byte > start_byte ? (end_byte - start_byte) : 0;
}

static void center_fit_for_width(const char *text, size_t target_width, char *out, size_t out_size) {
    if (!out || out_size == 0) return;

    if (!text || target_width == 0) {
        out[0] = '\0';
        return;
    }

    size_t text_width = utf8_display_width(text);

    if (text_width <= target_width) {
        size_t left_pad = (target_width - text_width) / 2;
        size_t used = 0;
        out[0] = '\0';

        while (left_pad > 0 && used + 1 < out_size) {
            out[used++] = ' ';
            left_pad--;
        }
        out[used] = '\0';

        size_t remain = out_size - used;
        if (remain > 1) {
            strncat(out, text, remain - 1);
        } else {
            out[used] = '\0';
        }
        return;
    }

    size_t start_col = (text_width - target_width) / 2;
    size_t start_byte = 0;
    size_t bytes = utf8_nbytes_for_column_range(text, start_col, target_width, &start_byte);

    if (bytes >= out_size) bytes = out_size - 1;
    if (bytes > 0) {
        memcpy(out, text + start_byte, bytes);
    }
    out[bytes] = '\0';
}

static void format_aligned_key(const char *key, int align_key, char *out, size_t out_size) {
    if (!out || out_size == 0) return;

    if (!key) {
        out[0] = '\0';
        return;
    }

    size_t key_len = strlen(key);
    size_t key_width = utf8_display_width(key);
    size_t target_width = (align_key > 0) ? (size_t)align_key : 0;
    size_t pad_spaces = (target_width > key_width) ? (target_width - key_width) : 0;

    size_t write_key_len = key_len;
    if (write_key_len >= out_size) write_key_len = out_size - 1;
    memcpy(out, key, write_key_len);
    out[write_key_len] = '\0';

    size_t used = write_key_len;
    while (pad_spaces > 0 && used + 1 < out_size) {
        out[used++] = ' ';
        pad_spaces--;
    }
    out[used] = '\0';
}

static bool env_falsey(const char *value) {
    if (!value || !value[0]) return false;

    return strcmp(value, "0") == 0 ||
           strcasecmp(value, "false") == 0 ||
           strcasecmp(value, "no") == 0 ||
           strcasecmp(value, "off") == 0;
}

static bool should_use_color(void) {
    const char *force_color = getenv("FORCE_COLOR");
    if (force_color && !env_falsey(force_color)) {
        return true;
    }

    const char *no_color = getenv("NO_COLOR");
    if (no_color && no_color[0]) {
        return false;
    }

    const char *clicolor = getenv("CLICOLOR");
    if (clicolor && env_falsey(clicolor)) {
        return false;
    }

    const char *term = getenv("TERM");
    if (term && strcmp(term, "dumb") == 0) {
        return false;
    }

    if (!isatty(STDOUT_FILENO)) {
        return false;
    }

    return true;
}

static void make_json_key(const char *label, char *out, size_t out_size) {
    if (!out || out_size == 0) return;

    if (!label || !label[0]) {
        snprintf(out, out_size, "unknown");
        return;
    }

    size_t j = 0;
    bool prev_underscore = false;

    for (size_t i = 0; label[i] && j + 1 < out_size; i++) {
        unsigned char c = (unsigned char)label[i];
        if (isalnum(c)) {
            out[j++] = (char)tolower(c);
            prev_underscore = false;
        } else if (!prev_underscore && j > 0) {
            out[j++] = '_';
            prev_underscore = true;
        }
    }

    while (j > 0 && out[j - 1] == '_') {
        j--;
    }

    if (j == 0) {
        snprintf(out, out_size, "unknown");
        return;
    }

    if (isdigit((unsigned char)out[0])) {
        if (j + 1 < out_size) {
            memmove(out + 1, out, j);
            out[0] = '_';
            j++;
        }
    }

    out[j] = '\0';
}

static void print_json_escaped(const char *value) {
    if (!value) {
        printf("\"\"");
        return;
    }

    putchar('"');
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        switch (*p) {
            case '"':
                printf("\\\"");
                break;
            case '\\':
                printf("\\\\");
                break;
            case '\b':
                printf("\\b");
                break;
            case '\f':
                printf("\\f");
                break;
            case '\n':
                printf("\\n");
                break;
            case '\r':
                printf("\\r");
                break;
            case '\t':
                printf("\\t");
                break;
            default:
                if (*p < 0x20) {
                    printf("\\u%04x", *p);
                } else {
                    putchar(*p);
                }
                break;
        }
    }
    putchar('"');
}

struct DistroLogo {
    const char *name;
    const char *const *lines;
    size_t line_count;
    unsigned char r;
    unsigned char g;
    unsigned char b;
};

static bool eq_icase(const char *a, const char *b) {
    if (!a || !b) return false;

    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void normalize_distro_token(const char *input, char *out, size_t out_size) {
    if (!out || out_size == 0) return;

    if (!input || !input[0]) {
        out[0] = '\0';
        return;
    }

    size_t j = 0;
    for (size_t i = 0; input[i] && j + 1 < out_size; i++) {
        unsigned char c = (unsigned char)input[i];
        if (isalnum(c)) {
            out[j++] = (char)tolower(c);
        }
    }
    out[j] = '\0';
}

static bool starts_with(const char *text, const char *prefix) {
    if (!text || !prefix) return false;

    size_t prefix_len = strlen(prefix);
    size_t text_len = strlen(text);
    if (prefix_len == 0 || text_len < prefix_len) return false;

    return strncmp(text, prefix, prefix_len) == 0;
}

static const char *resolve_distro_logo_alias(const char *name) {
    if (!name || !name[0]) return name;

    char normalized_name[128];
    normalize_distro_token(name, normalized_name, sizeof(normalized_name));
    if (!normalized_name[0]) return name;

    static const struct {
        const char *normalized_alias;
        const char *canonical;
    } alias_map[] = {
        {"ubuntu", "Ubuntu"},
        {"debian", "Debian"},
        {"arch", "Arch"},
        {"linuxmint", "Linux Mint"},
        {"mint", "Linux Mint"},
        {"pop", "Pop!_OS"},
        {"popos", "Pop!_OS"},
        {"kalilinux", "Kali Linux"},
        {"kali", "Kali Linux"},
        {"elementary", "elementary OS"},
        {"zorin", "Zorin OS"},
        {"manjaro", "Manjaro"},
        {"endeavouros", "EndeavourOS"},
        {"artix", "Artix Linux"},
        {"antergos", "Antergos"},
        {"fedora", "Fedora"},
        {"mxlinux", "MX Linux"},
        {"mx", "MX Linux"},
        {"peppermintos", "Peppermint OS"},
        {"peppermint", "Peppermint OS"},
        {"mageia", "Mageia"},
        {"almalinux", "AlmaLinux"},
        {"alma", "AlmaLinux"},
        {"centos", "CentOS"},
        {"opensuse", "openSUSE"},
        {"alpinelinux", "Alpine Linux"},
        {"alpine", "Alpine Linux"},
        {"gentoo", "Gentoo"},
        {"voidlinux", "Void Linux"},
        {"void", "Void Linux"},
        {"slackware", "Slackware"},
        {"solus", "Solus"},
    };

    for (size_t i = 0; i < sizeof(alias_map) / sizeof(alias_map[0]); i++) {
        if (starts_with(normalized_name, alias_map[i].normalized_alias)) {
            return alias_map[i].canonical;
        }
    }

    return name;
}

static bool terminal_supports_truecolor(void) {
    if (!should_use_color()) return false;

    const char *ct = getenv("COLORTERM");
    if (!ct) return false;

    return strstr(ct, "truecolor") != NULL || strstr(ct, "24bit") != NULL;
}

static int rgb_to_ansi256(unsigned char r, unsigned char g, unsigned char b) {
    int r6 = (int)((r * 5U) / 255U);
    int g6 = (int)((g * 5U) / 255U);
    int b6 = (int)((b * 5U) / 255U);
    return 16 + (36 * r6) + (6 * g6) + b6;
}

static void print_logo_lines(
    const char *const *lines,
    size_t line_count,
    bool color_enabled,
    bool use_truecolor,
    unsigned char r,
    unsigned char g,
    unsigned char b
) {
    if (!lines || line_count == 0) return;

    for (size_t i = 0; i < line_count; i++) {
        if (!color_enabled) {
            printf("%s\n", lines[i]);
            continue;
        }

        if (use_truecolor) {
            printf("\033[38;2;%u;%u;%um%s\033[0m\n", (unsigned)r, (unsigned)g, (unsigned)b, lines[i]);
        } else {
            int color_256 = rgb_to_ansi256(r, g, b);
            printf("\033[38;5;%dm%s\033[0m\n", color_256, lines[i]);
        }
    }
}

static void print_logo_line_inline(
    const char *line,
    bool color_enabled,
    bool use_truecolor,
    unsigned char r,
    unsigned char g,
    unsigned char b
) {
    if (!line) return;

    if (!color_enabled) {
        printf("%s", line);
        return;
    }

    if (use_truecolor) {
        printf("\033[38;2;%u;%u;%um%s\033[0m", (unsigned)r, (unsigned)g, (unsigned)b, line);
    } else {
        int color_256 = rgb_to_ansi256(r, g, b);
        printf("\033[38;5;%dm%s\033[0m", color_256, line);
    }
}

static void print_info_line_inline(
    const char *line,
    bool color_enabled,
    bool use_truecolor,
    unsigned char r,
    unsigned char g,
    unsigned char b
) {
    if (!line) return;

    if (!color_enabled) {
        printf("%s", line);
        return;
    }

    const char *sep = strstr(line, ": ");
    if (!sep) {
        printf("%s", line);
        return;
    }

    size_t key_len = (size_t)(sep - line);
    const char *rest = sep;

    if (use_truecolor) {
        printf("\033[38;2;%u;%u;%um%.*s\033[0m%s", (unsigned)r, (unsigned)g, (unsigned)b, (int)key_len, line, rest);
    } else {
        printf("\033[1;36m%.*s\033[0m%s", (int)key_len, line, rest);
    }
}

static void make_palette_row(int base, char *out, size_t out_size) {
    if (!out || out_size == 0) return;

    size_t used = 0;
    out[0] = '\0';

    for (int i = 0; i < 8; i++) {
        int color = base + i;
        int written = snprintf(
            out + used,
            out_size - used,
            "\033[48;5;%dm    \033[0m",
            color
        );
        if (written < 0 || (size_t)written >= out_size - used) {
            break;
        }
        used += (size_t)written;
    }
}

static void scale_ascii_line(
    const char *line,
    size_t keep_num,
    size_t keep_den,
    char *out,
    size_t out_size
) {
    if (!out || out_size == 0) return;

    if (!line) {
        out[0] = '\0';
        return;
    }

    if (keep_num == 0 || keep_den == 0) {
        snprintf(out, out_size, "%s", line);
        return;
    }

    size_t len = strlen(line);
    size_t pos = 0;
    size_t j = 0;

    size_t cp_index = 0;
    while (pos < len && j + 1 < out_size) {
        size_t consumed = 0;
        (void)utf8_codepoint_width(line + pos, len - pos, &consumed);
        if (consumed == 0) break;

        size_t repeat = scale_repeat_for_index(cp_index, keep_num, keep_den);
        for (size_t r = 0; r < repeat; r++) {
            if (j + consumed >= out_size) {
                out[j] = '\0';
                return;
            }
            memcpy(out + j, line + pos, consumed);
            j += consumed;
        }

        pos += consumed;
        cp_index++;
    }
    out[j] = '\0';

    while (j > 0 && out[j - 1] == ' ') {
        out[j - 1] = '\0';
        j--;
    }
}

static size_t build_scaled_logo_lines(
    const struct DistroLogo *logo,
    size_t scale_num,
    size_t scale_den,
    char out_storage[][MAX_CAPTURE_LINE_LEN],
    const char *out_lines[],
    size_t out_cap
) {
    if (!logo || !logo->lines || logo->line_count == 0 || !out_storage || !out_lines || out_cap == 0) {
        return 0;
    }

    size_t keep_num = scale_num;
    size_t keep_den = scale_den;
    if (keep_num == 0 || keep_den == 0) {
        keep_num = 1;
        keep_den = 1;
    }

    size_t count = 0;
    for (size_t i = 0; i < logo->line_count && count < out_cap; i++) {
        size_t copies = scale_repeat_for_index(i, keep_num, keep_den);
        for (size_t copy = 0; copy < copies && count < out_cap; copy++) {
            scale_ascii_line(
                logo->lines[i],
                keep_num,
                keep_den,
                out_storage[count],
                MAX_CAPTURE_LINE_LEN
            );
            out_lines[count] = out_storage[count];
            count++;
        }
    }

    if (count == 0 && logo->line_count > 0) {
        scale_ascii_line(logo->lines[0], keep_num, keep_den, out_storage[0], MAX_CAPTURE_LINE_LEN);
        out_lines[0] = out_storage[0];
        count = 1;
    }

    return count;
}

static size_t line_array_max_width(const char *const *lines, size_t line_count);

static size_t scaled_dimension(size_t source, size_t num, size_t den) {
    if (source == 0 || den == 0) return 0;
    size_t scaled = (source * num) / den;
    if (scaled == 0) scaled = 1;
    return scaled;
}

static void choose_logo_scale(
    const struct DistroLogo *logo,
    int terminal_width,
    int terminal_height,
    bool reserve_info_column,
    bool allow_upscale,
    size_t *out_num,
    size_t *out_den
) {
    if (!out_num || !out_den) return;

    *out_num = 1;
    *out_den = 1;

    if (!logo || !logo->lines || logo->line_count == 0) return;

    size_t src_w = line_array_max_width(logo->lines, logo->line_count);
    size_t src_h = logo->line_count;
    if (src_w == 0 || src_h == 0) return;

    size_t max_w = (terminal_width > 0) ? (size_t)terminal_width : src_w;
    size_t max_h = (terminal_height > 0) ? (size_t)terminal_height : src_h;

    if (max_h > 2) max_h -= 2;

    if (reserve_info_column) {
        const size_t min_right = 56;
        const size_t gap = 3;
        if (max_w > min_right + gap) {
            max_w -= (min_right + gap);
        } else if (max_w > gap) {
            max_w -= gap;
        }
    }

    if (max_w == 0) max_w = 1;
    if (max_h == 0) max_h = 1;

    const size_t den = 8;
    size_t num_w = (max_w * den) / src_w;
    size_t num_h = (max_h * den) / src_h;
    size_t num = num_w < num_h ? num_w : num_h;

    if (num == 0) num = 1;
    if (!allow_upscale && num > den) num = den;
    if (num > 64) num = 64;

    while ((scaled_dimension(src_w, num, den) > max_w || scaled_dimension(src_h, num, den) > max_h) && num > 1) {
        num--;
    }

    size_t g = gcd_size_t(num, den);
    *out_num = num / g;
    *out_den = den / g;
}

static size_t line_array_max_width(const char *const *lines, size_t line_count) {
    if (!lines || line_count == 0) return 0;

    size_t max_width = 0;
    for (size_t i = 0; i < line_count; i++) {
        size_t width = utf8_display_width(lines[i]);
        if (width > max_width) max_width = width;
    }

    return max_width;
}

static void append_wrapped_line(
    const char *text,
    size_t width,
    char out_lines[][MAX_CAPTURE_LINE_LEN],
    size_t *out_count,
    size_t out_cap
) {
    if (!text || !out_count || width == 0) return;

    ensure_utf8_locale();

    size_t len = strlen(text);
    size_t pos = 0;

    if (len == 0) {
        if (*out_count < out_cap) {
            out_lines[*out_count][0] = '\0';
            (*out_count)++;
        }
        return;
    }

    while (pos < len && *out_count < out_cap) {
        size_t remaining = len - pos;
        size_t chunk = utf8_nbytes_for_columns(text + pos, width);
        if (chunk == 0 && remaining > 0) {
            chunk = 1;
        }

        size_t split = chunk;
        if (split < remaining) {
            size_t last_space = 0;
            for (size_t i = 0; i < split; i++) {
                unsigned char c = (unsigned char)text[pos + i];
                if (c < 128 && isspace(c)) {
                    last_space = i;
                }
            }
            if (last_space > 0) {
                split = last_space;
            }
        }

        size_t copy_len = split < (MAX_CAPTURE_LINE_LEN - 1) ? split : (MAX_CAPTURE_LINE_LEN - 1);
        memcpy(out_lines[*out_count], text + pos, copy_len);
        out_lines[*out_count][copy_len] = '\0';

        while (copy_len > 0 && out_lines[*out_count][copy_len - 1] == ' ') {
            out_lines[*out_count][copy_len - 1] = '\0';
            copy_len--;
        }

        (*out_count)++;
        pos += split;
        while (pos < len) {
            unsigned char c = (unsigned char)text[pos];
            if (c < 128 && isspace(c)) {
                pos++;
                continue;
            }
            break;
        }
    }
}

static const struct DistroLogo *find_logo_for_distro(const char *distro);

static const char *const logo_ubuntu[] = {
    "            .-/+oossssoo+\\-.",
    "        ´:+ssssssssssssssssss+:`",
    "      -+ssssssssssssssssssyyssss+-",
    "    .ossssssssssssssssssdMMMNysssso.",
    "   /ssssssssssshdmmNNmmyNMMMMhssssss\\",
    "  +ssssssssshmydMMMMMMMNddddyssssssss+",
    " /sssssssshNMMMyhhyyyyhmNMMMNhssssssss\\",
    ".ssssssssdMMMNhsssssssssshNMMMdssssssss.",
    "+sssshhhyNMMNyssssssssssssyNMMMysssssss+",
    "ossyNMMMNyMMhsssssssssssssshmmmhssssssso",
    "ossyNMMMNyMMhsssssssssssssshmmmhssssssso",
    "+sssshhhyNMMNyssssssssssssyNMMMysssssss+",
    ".ssssssssdMMMNhsssssssssshNMMMdssssssss.",
    " \\sssssssshMMMyhhyyyyhdNMMMNhssssssss/",
    "  +sssssssssdmydMMMMMMMMddddyssssssss+",
    "   \\ssssssssssshdmNNNNmyNMMMMhssssss/",
    "    .ossssssssssssssssssdMMMNysssso.",
    "      -+sssssssssssssssssyyyssss+-",
    "        `:+ssssssssssssssssss+:`",
    "            .-\\+oossssoo+/-.",
};

static const char *const logo_debian[] = {
    "       _,met$$$$$gg.",
    "    ,g$$$$$$$$$$$$$$$P.",
    "  ,g$$P\"        \"\"\"Y$$.\".",
    " ,$$P'              `$$$.",
    "',$$P       ,ggs.     `$$b:",
    "`d$$'     ,$P\"'   .    $$$",
    " $$P      d$'     ,    $$P",
    " $$:      $$.   -    ,d$$'",
    " $$;      Y$b._   _,d$P'",
    " Y$$.    `.``\"Y$$$$P\"'",
    " `$$b      \"-.__",
    "  `Y$$",
    "   `Y$$.",
    "     `$$b.",
    "       `Y$$b.",
    "          `\"Y$b._",
    "              `\"\"\"",
};

static const char *const logo_arch[] = {
    "                   -`",
    "                  .o+`",
    "                 `ooo/",
    "                `+oooo:",
    "               `+oooooo:",
    "               -+oooooo+:",
    "             `/:-:++oooo+:",
    "            `/++++/+++++++:",
    "           `/++++++++++++++:",
    "          `/+++oooooooooooo/`",
    "         ./ooosssso++osssssso+`",
    "        .oossssso-````/ossssss+`",
    "       -osssssso.      :ssssssso.",
    "      :osssssss/        osssso+++.",
    "     /ossssssss/        +ssssooo/-",
    "   `/ossssso+/:-        -:/+osssso+-",
    "  `+sso+:-`                 `.-/+oso:",
    " `++:.                           `-/+/",
    " .`                                 `/",
};

static const char *const logo_fedora[] = {
    "             .',;::::;,'.",
    "         .';:cccccccccccc:;,.",
    "      .;cccccccccccccccccccccc;.",
    "    .:cccccccccccccccccccccccccc:.",
    "  .;ccccccccccccc;.:dddl:.;ccccccc;.",
    " .:ccccccccccccc;OWMKOOXMWd;ccccccc:.",
    ".:ccccccccccccc;KMMc;cc;xMMc;ccccccc:.",
    ",cccccccccccccc;MMM.;cc;;WW:;cccccccc,",
    ":cccccccccccccc;MMM.;cccccccccccccccc:",
    ":ccccccc;oxOOOo;MMM0OOk.;cccccccccccc:",
    "cccccc;0MMKxdd:;MMMkddc.;cccccccccccc;",
    "ccccc;XM0';cccc;MMM.;cccccccccccccccc'",
    "ccccc;MMo;ccccc;MMW.;ccccccccccccccc;",
    "ccccc;0MNc.ccc.xMMd;ccccccccccccccc;",
    "cccccc;dNMWXXXWM0:;cccccccccccccc:,,",
    "cccccccc;.:odl:.;cccccccccccccc:,,.",
    ":cccccccccccccccccccccccccccc:'.",
    ".:cccccccccccccccccccccc:;,..",
    "  '::cccccccccccccc::;,.",
};

static const char *const logo_alpine[] = {
    "       .hddddddddddddddddddddddh.",
    "      :dddddddddddddddddddddddddd:",
    "     /dddddddddddddddddddddddddddd/",
    "    +dddddddddddddddddddddddddddddd+",
    "  `sdddddddddddddddddddddddddddddddds`",
    " `ydddddddddddd++hdddddddddddddddddddy`",
    ".hddddddddddd+`  `+ddddh:-sdddddddddddh.",
    "hdddddddddd+`      `+y:    .sddddddddddh",
    "ddddddddh+`   `//`   `.`     -sddddddddd",
    "ddddddh+`   `/hddh/`   `:s-    -sddddddd",
    "ddddh+`   `/+/dddddh/`   `+s-    -sddddd",
    "ddd+`   `/o` :dddddddh/`   `oy-    .yddd",
    "hdddyo+ohddyosdddddddddho+oydddy++ohdddh",
    ".hddddddddddddddddddddddddddddddddddddh.",
    " `yddddddddddddddddddddddddddddddddddy`",
    "  `sdddddddddddddddddddddddddddddddds`",
    "    +dddddddddddddddddddddddddddddd+",
    "     /dddddddddddddddddddddddddddd/",
    "      :dddddddddddddddddddddddddd:",
    "       .hddddddddddddddddddddddh.",
};

static const char *const logo_manjaro[] = {
    "██████████████████  ████████",
    "██████████████████  ████████",
    "██████████████████  ████████",
    "██████████████████  ████████",
    "████████            ████████",
    "████████  ████████  ████████",
    "████████  ████████  ████████",
    "████████  ████████  ████████",
    "████████  ████████  ████████",
    "████████  ████████  ████████",
    "████████  ████████  ████████",
    "████████  ████████  ████████",
    "████████  ████████  ████████",
    "████████  ████████  ████████",
};

static const char *const logo_mint[] = {
    "             ...-:::::-...",
    "          .-MMMMMMMMMMMMMMM-.",
    "      .-MMMM`..-:::::::-..`MMMM-.",
    "    .:MMMM.:MMMMMMMMMMMMMMM:.MMMM:.",
    "   -MMM-M---MMMMMMMMMMMMMMMMMMM.MMM-",
    " `:MMM:MM`  :MMMM:....::-...-MMMM:MMM:`",
    " :MMM:MMM`  :MM:`  ``    ``  `:MMM:MMM:",
    ".MMM.MMMM`  :MM.  -MM.  .MM-  `MMMM.MMM.",
    ":MMM:MMMM`  :MM.  -MM-  .MM:  `MMMM-MMM:",
    ":MMM:MMMM`  :MM.  -MM-  .MM:  `MMMM:MMM:",
    ":MMM:MMMM`  :MM.  -MM-  .MM:  `MMMM-MMM:",
    ".MMM.MMMM`  :MM:--:MM:--:MM:  `MMMM.MMM.",
    " :MMM:MMM-  `-MMMMMMMMMMMM-`  -MMM-MMM:",
    "  :MMM:MMM:`                `:MMM:MMM:",
    "   .MMM.MMMM:--------------:MMMM.MMM.",
    "     '-MMMM.-MMMMMMMMMMMMMMM-.MMMM-'",
    "       '.-MMMM``--:::::--``MMMM-.'",
    "            '-MMMMMMMMMMMMM-'",
    "               ``-:::::-``",
};

static const char *const logo_pop[] = {
    "             /////////////",
    "         /////////////////////",
    "      ///////*767////////////////",
    "    //////7676767676*//////////////",
    "   /////76767//7676767//////////////",
    "  /////767676///*76767///////////////",
    " ///////767676///76767.///7676*///////",
    "/////////767676//76767///767676////////",
    "//////////76767676767////76767/////////",
    "///////////76767676//////7676//////////",
    "////////////,7676,///////767///////////",
    "/////////////*7676///////76////////////",
    "///////////////7676////////////////////",
    " ///////////////7676///767////////////",
    "  //////////////////////'////////////",
    "   //////.7676767676767676767,//////",
    "    /////767676767676767676767/////",
    "      ///////////////////////////",
    "         /////////////////////",
    "             /////////////",
};

static const char *const logo_kali[] = {
    "..............",
    "            ..,;:ccc,.",
    "          ......''';lxO.",
    ".....''''..........,:ld;",
    "           .';;;:::;,,.x,",
    "      ..'''.            0Xxoc:,.  ...",
    "  ....                ,ONkc;,;cokOdc',.",
    " .                   OMo           ':ddo.",
    "                    dMc               :OO;",
    "                    0M.                 .:o.",
    "                    ;Wd",
    "                     ;XO,",
    "                       ,d0Odlc;,.",
    "                           ..',;:cdOOd::,.",
    "                                    .:d;.':;.",
    "                                       'd,  .'",
    "                                         ;l   ..",
    "                                          .o",
    "                                            c",
    "                                            .'",
    "                                             .",
};

static const char *const logo_elementary[] = {
    "         eeeeeeeeeeeeeeeee",
    "      eeeeeeeeeeeeeeeeeeeeeee",
    "    eeeee  eeeeeeeeeeee   eeeee",
    "  eeee   eeeee       eee     eeee",
    " eeee   eeee          eee     eeee",
    "eee    eee            eee       eee",
    "eee   eee            eee        eee",
    "ee    eee           eeee       eeee",
    "ee    eee         eeeee      eeeeee",
    "ee    eee       eeeee      eeeee ee",
    "eee   eeee   eeeeee      eeeee  eee",
    "eee    eeeeeeeeee     eeeeee    eee",
    " eeeeeeeeeeeeeeeeeeeeeeee    eeeee",
    "  eeeeeeee eeeeeeeeeeee      eeee",
    "    eeeee                 eeeee",
    "      eeeeeee         eeeeeee",
    "         eeeeeeeeeeeeeeeee",
};

static const char *const logo_zorin[] = {
    "        `osssssssssssssssssssso`",
    "       .osssssssssssssssssssssso.",
    "      .+oooooooooooooooooooooooo+.",
    "",
    "",
    "  `::::::::::::::::::::::.         .:`",
    " `+ssssssssssssssssss+:.`     `.:+ssso`",
    ".ossssssssssssssso/.       `-+ossssssso.",
    "ssssssssssssso/-`      `-/osssssssssssss",
    ".ossssssso/-`      .-/ossssssssssssssso.",
    " `+sss+:.      `.:+ssssssssssssssssss+`",
    "  `:.         .::::::::::::::::::::::`",
    "",
    "",
    "      .+oooooooooooooooooooooooo+.",
    "       -osssssssssssssssssssssso-",
    "        `osssssssssssssssssssso`",
};

static const char *const logo_endeavouros[] = {
    "                     ./o.",
    "                   ./sssso-",
    "                 `:osssssss+-",
    "               `:+sssssssssso/.",
    "             `-/osssssssssssssso/.",
    "           `-/+sssssssssssssssso+:`",
    "         `-:/+sssssssssssssssssso+/.",
    "       `.://osssssssssssssssssssso++-",
    "      .://+ssssssssssssssssssssssso++:",
    "    .:///ossssssssssssssssssssssssso++:",
    "  `:////ssssssssssssssssssssssssssso+++.",
    "`-////+sssssssssssssssssssssssssssso++++-",
    " `..-+oosssssssssssssssssssssssso+++++/`",
    "   ./++++++++++++++++++++++++++++++/:.",
    "  `:::::::::::::::::::::::::------``",
};

static const char *const logo_artix[] = {
    "                   '",
    "                  'o'",
    "                 'ooo'",
    "                'ooxoo'",
    "               'ooxxxoo'",
    "              'oookkxxoo'",
    "             'oiioxkkxxoo'",
    "            ':;:iiiioxxxoo'",
    "               `'.;::ioxxoo'",
    "          '-.      `':;jiooo'",
    "         'oooio-..     `'i:io'",
    "        'ooooxxxxoio:,.   `'-;'",
    "       'ooooxxxxxkkxoooIi:-.  `'",
    "      'ooooxxxxxkkkkxoiiiiiji'",
    "     'ooooxxxxxkxxoiiii:'`     .i'",
    "    'ooooxxxxxoi:::'`       .;ioxo'",
    "   'ooooxooi::'`         .:iiixkxxo'",
    "  'ooooi:'`                `'';ioxxo'",
    " 'i:'`                          '':io'",
    "'`                                   `'",
};

static const char *const logo_opensuse[] = {
    "           .;ldkO0000Okdl;.",
    "       .;d00xl:^''''''^:ok00d;.",
    "     .d00l'                'o00d.",
    "   .d0Kd'  Okxol:;,.          :O0d.",
    "  .OKKKK0kOKKKKKKKKKKOxo:,      lKO.",
    " ,0KKKKKKKKKKKKKKKK0P^,,,^dx:    ;00,",
    ".OKKKKKKKKKKKKKKKKk'.oOPPb.'0k.   cKO.",
    ":KKKKKKKKKKKKKKKK: kKx..dd lKd   'OK:",
    "dKKKKKKKKKKOx0KKKd ^0KKKO' kKKc   dKd",
    "dKKKKKKKKKKK;.;oOKx,..^..;kKKK0.  dKd",
    ":KKKKKKKKKKK0o;...^cdxxOK0O/^^'  .0K:",
    " kKKKKKKKKKKKKK0x;,,......,;od  lKk",
    " '0KKKKKKKKKKKKKKKKKKK00KKOo^  c00'",
    "  'kKKOxddxkOO00000Okxoc;''   .dKk'",
    "    l0Ko.                    .c00l'",
    "     'l0Kk:.              .;xK0l'",
    "        'lkK0xl:;,,,,;:ldO0kl'",
    "            '^:ldxkkkkxdl:^'",
};

static const char *const logo_gentoo[] = {
    "         -/oyddmdhs+:.",
    "     -odNMMMMMMMMNNmhy+-`",
    "   -yNMMMMMMMMMMMNNNmmdhy+-",
    " `omMMMMMMMMMMMMNmdmmmmddhhy/`",
    " omMMMMMMMMMMMNhhyyyohmdddhhhdo`",
    ".ydMMMMMMMMMMdhs++so/smdddhhhhdm+`",
    " oyhdmNMMMMMMMNdyooydmddddhhhhyhNd.",
    "  :oyhhdNNMMMMMMMNNNmmdddhhhhhyymMh",
    "    .:+sydNMMMMMNNNmmmdddhhhhhhmMmy",
    "       /mMMMMMMNNNmmmdddhhhhhmMNhs:",
    "    `oNMMMMMMMNNNmmmddddhhdmMNhs+`",
    "  `sNMMMMMMMMNNNmmmdddddmNMmhs/.",
    " /NMMMMMMMMNNNNmmmdddmNMNdso:`",
    "+MMMMMMMNNNNNmmmmdmNMNdso/-",
    "yMMNNNNNNNmmmmmNNMmhs+/-`",
    "/hMMNNNNNNNNMNdhs++/-`",
    "`/ohdmmddhys+++/:.`",
    "  `-//////:--.",
};

static const char *const logo_almalinux[] = {
    "         'c:.",
    "        lkkkx, ..       ..   ,cc,",
    "        okkkk:ckkx'  .lxkkx.okkkkd",
    "        .:llcokkx'  :kkkxkko:xkkd,",
    "      .xkkkkdood:  ;kx,  .lkxlll;",
    "       xkkx.       xk'     xkkkkk:",
    "       'xkx.       xd      .....,.",
    "      .. :xkl'     :c      ..''..",
    "    .dkx'  .:ldl:'. '  ':lollldkkxo;",
    "  .''lkkko'                     ckkkx.",
    "'xkkkd:kkd.       ..  ;'        :kkxo.",
    ",xkkkd;kk'      ,d;    ld.   ':dkd::cc,",
    " .,,.;xkko'.';lxo.      dx,  :kkk'xkkkkc",
    "     'dkkkkkxo:.        ;kx  .kkk:;xkkd.",
    "       .....   .;dk:.   lkk.  :;,",
    "             :kkkkkkkdoxkkx",
    "              ,c,,;;;:xkkd.",
    "                ;kkkkl...",
    "                ;kkkkl",
    "                 ,od;",
};

    static const char *const logo_antergos[] = {
        "              `.-/::/-``",
        "            .-/osssssssso/.",
        "           :osyysssssssyyys+-",
        "        `.+yyyysssssssssyyyyy+.",
        "       `/syyyyyssssssssssyyyyys-`",
        "      `/yhyyyyysss++ssosyyyyhhy/`",
        "     .ohhhyyysoo++/+oso+syy+shhhho.",
        "    .shhhhysoo++//+sss+++yyy+shhhhs.",
        "   -yhhhhs+++++++ossso+++yyys+ohhddy:",
        "  -yddhhyo+++++osyyss++++yyyyooyhdddy-",
        " .yddddhso++osyyyyys+++++yyhhsoshddddy`",
        "`odddddhyosyhyyyyyy++++++yhhhyosddddddo",
        ".dmdddddhhhhhhhyyyo+++++shhhhhohddddmmh.",
        "ddmmdddddhhhhhhhso++++++yhhhhhhdddddmmdy",
        "dmmmdddddddhhhyso++++++shhhhhddddddmmmmh",
        "-dmmmdddddddhhyso++++oshhhhdddddddmmmmd-",
        ".smmmmddddddddhhhhhhhhhdddddddddmmmms.",
        "   `+ydmmmdddddddddddddddddddmmmmdy/.",
        "      `.:+ooyyddddddddddddyyso+:.`",
    };

    static const char *const logo_centos[] = {
        "                 ..",
        "               .PLTJ.",
        "              <><><><>",
        "     KKSSV' 4KKK LJ KKKL.'VSSKK",
        "     KKV' 4KKKKK LJ KKKKAL 'VKK",
        "     V' ' 'VKKKK LJ KKKKV' ' 'V",
        "     .4MA.' 'VKK LJ KKV' '.4Mb.",
        "   . KKKKKA.' 'V LJ V' '.4KKKKK .",
        " .4D KKKKKKKA.'' LJ ''.4KKKKKKK FA.",
        "<QDD ++++++++++++  ++++++++++++ GFD>",
        " 'VD KKKKKKKK'.. LJ ..'KKKKKKKK FV",
        "   ' VKKKKK'. .4 LJ K. .'KKKKKV '",
        "     'VK'. .4KK LJ KKA. .'KV'",
        "     A. . .4KKKK LJ KKKKA. . .4",
        "     KKA. 'KKKKK LJ KKKKK' .4KK",
        "     KKSSA. VKKK LJ KKKKV .4SSKK",
        "              <><><><>",
        "               'MKKM'",
        "                 ''",
    };

    static const char *const logo_mageia[] = {
        "        .°°.",
        "         °°   .°°.",
        "         .°°°. °°",
        "         .   .",
        "          °°° .°°°.",
        "      .°°°.   '___'",
        "     .'___'        .",
        "   :dkxc;'.  ..,cxkd;",
        " .dkk. kkkkkkkkkk .kkd.",
        ".dkk.  ';cloolc;.  .kkd",
        "ckk.                .kk;",
        "xO:                  cOd",
        "xO:                  lOd",
        "lOO.                .OO:",
        ".k00.              .00x",
        " .k00;            ;00O.",
        "  .lO0Kc;,,,,,,;c0KOc.",
        "     ;d00KKKKKK00d;",
        "        .,KKKK,.",
    };

    static const char *const logo_mx[] = {
        "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMNMMMMMMMMM",
        "MMMMMMMMMMNs..yMMMMMMMMMMMMMm: +NMMMMMMM",
        "MMMMMMMMMN+    :mMMMMMMMMMNo` -dMMMMMMMM",
        "MMMMMMMMMMMs.   `oNMMMMMMh- `sNMMMMMMMMM",
        "MMMMMMMMMMMMN/    -hMMMN+  :dMMMMMMMMMMM",
        "MMMMMMMMMMMMMMh-    +ms. .sMMMMMMMMMMMMM",
        "MMMMMMMMMMMMMMMN+`   `  +NMMMMMMMMMMMMMM",
        "MMMMMMMMMMMMMMNMMd:    .dMMMMMMMMMMMMMMM",
        "MMMMMMMMMMMMm/-hMd-     `sNMMMMMMMMMMMMM",
        "MMMMMMMMMMNo`   -` :h/    -dMMMMMMMMMMMM",
        "MMMMMMMMMd:       /NMMh-   `+NMMMMMMMMMM",
        "MMMMMMMNo`         :mMMN+`   `-hMMMMMMMM",
        "MMMMMMh.            `oNMMd:    `/mMMMMMM",
        "MMMMm/                -hMd-      `sNMMMM",
        "MMNs`                   -          :dMMM",
        "Mm:                                 `oMM",
        "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM",
    };

    static const char *const logo_peppermint[] = {
        "               PPPPPPPPPPPPPP",
        "           PPPPMMMMMMMPPPPPPPPPPP",
        "         PPPPMMMMMMMMMMPPPPPPPPMMPP",
        "       PPPPPPPPMMMMMMMPPPPPPPPMMMMMPP",
        "     PPPPPPPPPPPPMMMMMMPPPPPPPMMMMMMMPP",
        "    PPPPPPPPPPPPMMMMMMMPPPPMPMMMMMMMMMPP",
        "   PPMMMMPPPPPPPPPMMMPPPPPMMMMMMMPMMPPPP",
        "   PMMMMMMMMMMPPPPPPMMPPPPPMMMMMMPPPPPPPP",
        "  PMMMMMMMMMMMMPPPPPMMPMPMMPMMPMMPPPPPPPPPP",
        "  PMMMMMMMMMMMMMMPPMPMMMPPPPPPPPPPPPPPPPP",
        "  PMMMPPPPPPPPPPPPPPPPPPPPPPPPPPPPPMMMMMP",
        "  PPPPPPPPPPPPPPPMMMPMPMMMMMMMMMMMMMMMMPP",
        "  PPPPPPPPPPPMMPMMPPPPMMPPPPPMMMMMMMMMMMPP",
        "   PPPPPPPPMMMMMMPPPPPMMPPPPPPPPPPMMMMMPP",
        "   PPPPMMPMMMMMMMPPPPPPMMPPPPPPPPPPPMMMMPP",
        "    PPMMMMMMMMMPMPPPPMMMMMMPPPPPPPPPPPP",
        "     PPMMMMMMMPPPPPPPMMMMMMPPPPPPPPPPP",
        "       PPMMMMPPPPPPPPPMMMMMMMMPPPPPPPP",
        "         PPMMPPPPPPPPMMMMMMMMMMPPPP",
        "           PPPPPPPPPPMMMMMMMMPPPP",
        "               PPPPPPPPPPPPPP",
    };

    static const char *const logo_void[] = {
        "                __.;=====;.__",
        "            _.=+==++=++=+=+===;.",
        "             -=+++=+===+=+=+++++=_",
        "        .     -=:``     `--==+=++==.",
        "       _vi,    `            --+=++++:",
        "      .uvnvi.       _._       -==+==+.",
        "     .vvnvnI`    .;==|==;.     :|=||=|.",
        "+QmQQmpvvnv; _yYsyQQWUUQQQm #QmQ#:QQQWUV$QQm.",
        " -QQWQWpvvowZ?.wQQQE==<QWWQ/QWQW.QQWW(: jQWQE",
        "  -$QQQQmmU'  jQQQ@+=<QWQQ)mQQQ.mQQQC+;jWQQ@'",
        "   -$WQ8YnI:   QWQQwgQQWV`mWQQ.jQWQQgyyWW@!",
        "     -1vvnvv.     `~+++`        ++|+++",
        "      +vnvnnv,                 `-|===",
        "       +vnvnvns.           .      :=-",
        "        -Invnvvnsi..___..=sv=.     `",
        "          +Invnvnvnnnnnnnnvvnn;.",
        "            ~|Invnvnvvnvvvnnv}+`",
        "               -~|{*l}*|~",
    };

static const char *const logo_slackware[] = {
    "                  :::::::",
    "            :::::::::::::::::::",
    "         :::::::::::::::::::::::::",
    "       ::::::::cllcccccllllllll::::::",
    "    :::::::::lc               dc:::::::",
    "   ::::::::cl   clllccllll    oc:::::::::",
    "  ::::::::o   lc::::::::co   oc::::::::::",
    " ::::::::::o    cccclc:::::clcc::::::::::::",
    " :::::::::::lc        cclccclc:::::::::::::",
    "::::::::::::::lcclcc          lc::::::::::::",
    "::::::::::cclcc:::::lccclc     oc:::::::::::",
    "::::::::::o    l::::::::::l    lc:::::::::::",
    " :::::cll:o     clcllcccll     o:::::::::::",
    " :::::occ:o                  clc:::::::::::",
    "  ::::ocl:ccslclccclclccclclc:::::::::::::",
    "   :::oclcccccccccccccllllllllllllll:::::",
    "    ::lcc1lcccccccccccccccccccccccco::::",
    "      :::::::::::::::::::::::::::::::",
    "        ::::::::::::::::::::::::::::",
    "           ::::::::::::::::::::::",
    "                ::::::::::::",
};

static const char *const logo_solus[] = {
    "            -```````````",
    "          `-+/------------.`",
    "       .---:mNo---------------.",
    "     .-----yMMMy:---------------.",
    "   `------oMMMMMm/----------------`",
    "  .------/MMMMMMMN+----------------.",
    " .------/NMMMMMMMMm-+/--------------.",
    "`------/NMMMMMMMMMN-:mh/-------------`",
    ".-----/NMMMMMMMMMMM:-+MMd//oso/:-----.",
    "-----/NMMMMMMMMMMMM+--mMMMh::smMmyo:--",
    "----+NMMMMMMMMMMMMMo--yMMMMNo-:yMMMMd/.",
    ".--oMMMMMMMMMMMMMMMy--yMMMMMMh:-yMMMy-`",
    "`-sMMMMMMMMMMMMMMMMh--dMMMMMMMd:/Ny+y.",
    "`-/+osyhhdmmNNMMMMMm-/MMMMMMMmh+/ohm+",
    "  .------------:://+-/++++++oshddys:",
    "   -hhhhyyyyyyyyyyyhhhhddddhysssso-",
    "    `:ossssssyysssssssssssssssso:`",
    "      `:+ssssssssssssssssssss+-",
    "         `-/+ssssssssssso+/-`",
    "              `.-----..`",
};

static const char *const logo_generic[] = {
        "                            @@@@@@@@@@@@@@@@                              ",
        "                           @@@@@@@@@@@@@@@@@@@@                            ",
        "                          @@@@@@@@@@@@@@@@*#@@@@@                          ",
        "                         @@@@@@@@@@@@@@@@@.:=@@@@@                         ",
        "                         @@@@@@@@@@@@@@@@@@@@@@@@@@                        ",
        "                        @@@@@@@@@@@@@@@@@@@@@@@@@@@                        ",
        "                        @@@@@%@@@@@@@@#++*%@@@@@@@@                        ",
        "                        @@@=. .-@@@@@-     -@@@@@@@                        ",
        "                        @@#:#*=.*@@@+ :#*=- =@@@@@@                        ",
        "                        @@#=@%+-+@@@- %@@*#..@@@@@@@                       ",
        "                        @@@.*@%=-::==.*@@@#.:@@@@@@@                       ",
        "                        @@@@+:::::::::::=#@@@@@@@@@@                       ",
        "                        @@%-:::::::::::::::-=@@@@@@@                       ",
        "                         @@#-::::::::::-+**-+@@@@@@@@                      ",
        "                         @@@@*=--=+**+==-==:%@@%:+%@@@                     ",
        "                         @@#@%++++=--=*#*+: +@@@#-:@@@@                    ",
        "                       @@@# -@@#*+*%@#=.    .+@@@@@@@@@@                   ",
        "                      @@@%.  .#@@@@*.         -@@@@@@@@@@                  ",
        "                     @@@@.     ...             -@@@@@@@@@@                 ",
        "                    @@@@:                       #@@@@@@@@@@                ",
        "                  @@@@@%.                       :@@@@=@@@@@@@              ",
        "                 @@@@@@%.                        +@@@@-+@@@@@@             ",
        "                @@*+@@@-                         .+@@@@=-@@@@@@@           ",
        "               @@%*@@@:                            +@@@@=-@@@@@@           ",
        "              @@@@@@@-                             .*@@@@-+@@@@@@          ",
        "              @@@@@@+                               .@@@@%.%@@@@@@         ",
        "             @@@@@@%.                                *@@@@-+@@@@@@         ",
        "            @@@=@@@-                                 +@@@@+=@@@@@@@        ",
        "           @@@#+@@@.                                 +@@@@*+@@@@@@@        ",
        "          @@@@#+@@%                                  +@@@@-%@@@@@@@@       ",
        "          @@@@@-%@%                                  -@@%=:#@@@@@@@@       ",
        "          @@@@@%-+*                                  .+#%%%#+%@@@@@@       ",
        "          *+-=*%@+.                               .:-+@@@@@@@@@@@@@        ",
        "         *::::::*@@+..                           .%#+*@@@@@@@@@@@%         ",
        "        @%:::::::-%@@*..                         -%:::#@@@@@@@@#-::+       ",
        "       @@=:::::::::*@@@%:                        -#::::=%@@@%=-:::-@       ",
        "@%===++=-:::::::::::=@@@@%+.                     -#::::::---:::::::@@      ",
        "@#:::::::::::::::::::-%@@@@@-                    -@-:::::::::::::::+@@@    ",
        "@@+:::::::::::::::::::-#@@@@%                  ..-@+::::::::::::::::=*%%@@ ",
        " @@-::::::::::::::::::::*@@*:                .+@*=@*::::::::::::::::::::-*@",
        " @@-:::::::::::::::::::::++..:.            .-%@@+=@*::::::::::::::::::::-+@",
        "@@+:::::::::::::::::::::::*%.+%=:.....:::=#@@@@@.*@+::::::::::::::::--=*@@ ",
        "@=::::::::::::::::::::::::-@@.*@@@@@@@@@@@@@@@@*.@@-::::::::::::-=#@@@@    ",
        "@@@%#+-:::::::::::::::::::=@@=*@@@@@@@@@@@@@@@@+-@@-::::::::::+@@@@        ",
        "  @@@@@@@@%#*+-:::::::::::%@@@@@@@@@@@@@@@@@@@@@@@@%-::::::-#@@@           ",
        "           @@@@@@%%#*+++#@@@@@                  @@@@@%*+*#%@@@             ",
        "                 @@@@@@@@@@                       @@@@@@@@@@               ",
};

static const struct DistroLogo logos[] = {
    {"Ubuntu", logo_ubuntu, sizeof(logo_ubuntu) / sizeof(logo_ubuntu[0]), 233, 84, 32},
    {"Debian", logo_debian, sizeof(logo_debian) / sizeof(logo_debian[0]), 215, 10, 83},
    {"Arch", logo_arch, sizeof(logo_arch) / sizeof(logo_arch[0]), 23, 147, 209},
    {"Linux Mint", logo_mint, sizeof(logo_mint) / sizeof(logo_mint[0]), 135, 154, 57},
    {"Pop!_OS", logo_pop, sizeof(logo_pop) / sizeof(logo_pop[0]), 72, 198, 239},
    {"Kali Linux", logo_kali, sizeof(logo_kali) / sizeof(logo_kali[0]), 54, 99, 158},
    {"elementary OS", logo_elementary, sizeof(logo_elementary) / sizeof(logo_elementary[0]), 100, 85, 184},
    {"Zorin OS", logo_zorin, sizeof(logo_zorin) / sizeof(logo_zorin[0]), 20, 122, 255},
    {"Manjaro", logo_manjaro, sizeof(logo_manjaro) / sizeof(logo_manjaro[0]), 53, 171, 106},
    {"Antergos", logo_antergos, sizeof(logo_antergos) / sizeof(logo_antergos[0]), 76, 139, 245},
    {"EndeavourOS", logo_endeavouros, sizeof(logo_endeavouros) / sizeof(logo_endeavouros[0]), 127, 40, 170},
    {"Artix Linux", logo_artix, sizeof(logo_artix) / sizeof(logo_artix[0]), 54, 116, 191},
    {"MX Linux", logo_mx, sizeof(logo_mx) / sizeof(logo_mx[0]), 50, 128, 167},
    {"Peppermint OS", logo_peppermint, sizeof(logo_peppermint) / sizeof(logo_peppermint[0]), 205, 37, 50},
    {"Fedora", logo_fedora, sizeof(logo_fedora) / sizeof(logo_fedora[0]), 81, 117, 195},
    {"Mageia", logo_mageia, sizeof(logo_mageia) / sizeof(logo_mageia[0]), 35, 161, 207},
    {"AlmaLinux", logo_almalinux, sizeof(logo_almalinux) / sizeof(logo_almalinux[0]), 53, 77, 147},
    {"CentOS", logo_centos, sizeof(logo_centos) / sizeof(logo_centos[0]), 147, 89, 194},
    {"openSUSE", logo_opensuse, sizeof(logo_opensuse) / sizeof(logo_opensuse[0]), 115, 186, 37},
    {"Alpine Linux", logo_alpine, sizeof(logo_alpine) / sizeof(logo_alpine[0]), 14, 104, 173},
    {"Gentoo", logo_gentoo, sizeof(logo_gentoo) / sizeof(logo_gentoo[0]), 143, 122, 179},
    {"Void Linux", logo_void, sizeof(logo_void) / sizeof(logo_void[0]), 70, 186, 116},
    {"Slackware", logo_slackware, sizeof(logo_slackware) / sizeof(logo_slackware[0]), 47, 116, 193},
    {"Solus", logo_solus, sizeof(logo_solus) / sizeof(logo_solus[0]), 82, 148, 226},
};

static const struct DistroLogo *find_logo_for_distro(const char *distro) {
    if (distro && distro[0]) {
        const char *lookup = resolve_distro_logo_alias(distro);
        for (size_t i = 0; i < sizeof(logos) / sizeof(logos[0]); i++) {
            if (eq_icase(lookup, logos[i].name)) {
                return &logos[i];
            }
        }
    }

    static const struct DistroLogo fallback_logo = {
        "generic",
        logo_generic,
        sizeof(logo_generic) / sizeof(logo_generic[0]),
        186,
        189,
        182,
    };

    return &fallback_logo;
}

int get_terminal_width() {
    struct winsize w;
    // FIXME: no error handling
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
}

static int get_terminal_height() {
    struct winsize w;
    // FIXME: no error handling
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_row;
}

void print_info(const char *key, const char *format, int align_key, int align_value, ...) {
    (void)align_value;

    va_list args;
    va_start(args, align_value);

    if (g_capture_info) {
        char value_buffer[384];
        char line_buffer[MAX_CAPTURE_LINE_LEN];
        char key_buffer[64];
        char aligned_key[128];

        vsnprintf(value_buffer, sizeof(value_buffer), format, args);
        format_aligned_key(key, align_key, aligned_key, sizeof(aligned_key));
        size_t line_cap = sizeof(line_buffer);
        size_t prefix_len = strlen(aligned_key);

        if (prefix_len + 2 >= line_cap) {
            prefix_len = line_cap - 3;
        }

        memcpy(line_buffer, aligned_key, prefix_len);
        line_buffer[prefix_len] = ':';
        line_buffer[prefix_len + 1] = ' ';

        size_t available = line_cap - (prefix_len + 2);
        size_t value_len = strnlen(value_buffer, available > 0 ? (available - 1) : 0);
        if (available > 0 && value_len > 0) {
            memcpy(line_buffer + prefix_len + 2, value_buffer, value_len);
        }
        line_buffer[prefix_len + 2 + value_len] = '\0';
        make_json_key(key, key_buffer, sizeof(key_buffer));
        if (key_buffer[0] == '\0' || strcmp(key_buffer, "unknown") == 0) {
            if (g_info_kv_count > 0 && g_info_keys[g_info_kv_count - 1][0] != '\0') {
                strncpy(key_buffer, g_info_keys[g_info_kv_count - 1], sizeof(key_buffer) - 1);
                key_buffer[sizeof(key_buffer) - 1] = '\0';
            }
        }

        if (g_info_line_count < MAX_CAPTURE_LINES) {
            strncpy(g_info_lines[g_info_line_count], line_buffer, MAX_CAPTURE_LINE_LEN - 1);
            g_info_lines[g_info_line_count][MAX_CAPTURE_LINE_LEN - 1] = '\0';
            g_info_line_count++;
        }

        if (g_info_kv_count < MAX_CAPTURE_LINES) {
            strncpy(g_info_keys[g_info_kv_count], key_buffer, sizeof(g_info_keys[g_info_kv_count]) - 1);
            g_info_keys[g_info_kv_count][sizeof(g_info_keys[g_info_kv_count]) - 1] = '\0';

            strncpy(g_info_values[g_info_kv_count], value_buffer, sizeof(g_info_values[g_info_kv_count]) - 1);
            g_info_values[g_info_kv_count][sizeof(g_info_values[g_info_kv_count]) - 1] = '\0';
            g_info_kv_count++;
        }
    } else {
        char aligned_key[128];
        format_aligned_key(key, align_key, aligned_key, sizeof(aligned_key));
        printf("%s: ", aligned_key);
        vprintf(format, args);
        printf("\n");
    }

    va_end(args);
}

void begin_info_capture(void) {
    g_capture_info = true;
    g_info_line_count = 0;
    g_info_kv_count = 0;
}

void end_info_capture(void) {
    g_capture_info = false;
}

void render_json_output(const char *user_host) {
    char base_keys[MAX_CAPTURE_LINES][64];
    size_t base_counts[MAX_CAPTURE_LINES] = {0};
    size_t base_key_count = 0;

    printf("{\n");

    printf("  \"user_host\": ");
    print_json_escaped(user_host ? user_host : "");

    for (size_t i = 0; i < g_info_kv_count; i++) {
        char base_key[64];
        strncpy(base_key, g_info_keys[i], sizeof(base_key) - 1);
        base_key[sizeof(base_key) - 1] = '\0';
        if (base_key[0] == '\0') {
            snprintf(base_key, sizeof(base_key), "unknown");
        }

        size_t idx = base_key_count;
        for (size_t j = 0; j < base_key_count; j++) {
            if (strcmp(base_key, base_keys[j]) == 0) {
                idx = j;
                break;
            }
        }

        if (idx == base_key_count && base_key_count < MAX_CAPTURE_LINES) {
            strncpy(base_keys[base_key_count], base_key, sizeof(base_keys[base_key_count]) - 1);
            base_keys[base_key_count][sizeof(base_keys[base_key_count]) - 1] = '\0';
            base_counts[base_key_count] = 0;
            base_key_count++;
        }

        size_t occurrence = 1;
        if (idx < MAX_CAPTURE_LINES) {
            base_counts[idx]++;
            occurrence = base_counts[idx];
        }

        char unique_key[64];
        strncpy(unique_key, base_key, sizeof(unique_key) - 1);
        unique_key[sizeof(unique_key) - 1] = '\0';

        if (occurrence > 1) {
            char suffixed[64];
            char suffix_num[32];
            snprintf(suffix_num, sizeof(suffix_num), "%zu", occurrence);

            size_t suffix_len = strlen(suffix_num);
            size_t max_key_len = 0;
            if (suffix_len + 1 < sizeof(suffixed)) {
                max_key_len = sizeof(suffixed) - suffix_len - 2;
            }

            snprintf(
                suffixed,
                sizeof(suffixed),
                "%.*s_%s",
                (int)max_key_len,
                base_key,
                suffix_num
            );
            strncpy(unique_key, suffixed, sizeof(unique_key) - 1);
            unique_key[sizeof(unique_key) - 1] = '\0';
        }

        printf(",\n  \"");
        printf("%s", unique_key);
        printf("\": ");
        print_json_escaped(g_info_values[i]);
    }

    printf("\n}\n");
}

void render_fetch_panel(const char *distro, const char *user_host) {
    bool color_enabled = should_use_color();
    bool use_truecolor = terminal_supports_truecolor();
    const struct DistroLogo *logo = find_logo_for_distro(distro);
    int terminal_width = get_terminal_width();
    int terminal_height = get_terminal_height();
    size_t scale_num = 1;
    size_t scale_den = 1;

    if (terminal_width <= 0) terminal_width = 80;
    if (terminal_height <= 0) terminal_height = 24;

    choose_logo_scale(logo, terminal_width, terminal_height, true, false, &scale_num, &scale_den);

    char scaled_logo_storage[MAX_CAPTURE_LINES][MAX_CAPTURE_LINE_LEN];
    const char *scaled_logo_lines[MAX_CAPTURE_LINES];
    size_t scaled_logo_count = build_scaled_logo_lines(
        logo,
        scale_num,
        scale_den,
        scaled_logo_storage,
        scaled_logo_lines,
        MAX_CAPTURE_LINES
    );
    size_t left_width = line_array_max_width(scaled_logo_lines, scaled_logo_count);

    if ((size_t)terminal_width <= left_width + 8) {
        char clipped_line[MAX_CAPTURE_LINE_LEN];

        for (size_t i = 0; i < scaled_logo_count; i++) {
            center_fit_for_width(scaled_logo_lines[i], (size_t)terminal_width, clipped_line, sizeof(clipped_line));
            print_logo_line_inline(clipped_line, color_enabled, use_truecolor, logo->r, logo->g, logo->b);
            printf("\n");
        }

        if (user_host && user_host[0]) {
            truncate_for_width(user_host, (size_t)terminal_width, clipped_line, sizeof(clipped_line));
            printf("%s\n", clipped_line);
        }
        for (size_t i = 0; i < g_info_line_count; i++) {
            truncate_for_width(g_info_lines[i], (size_t)terminal_width, clipped_line, sizeof(clipped_line));
            print_info_line_inline(clipped_line, color_enabled, use_truecolor, logo->r, logo->g, logo->b);
            printf("\n");
        }

        if (color_enabled && terminal_width >= 16) {
            char palette_row_1[256];
            char palette_row_2[256];
            make_palette_row(0, palette_row_1, sizeof(palette_row_1));
            make_palette_row(8, palette_row_2, sizeof(palette_row_2));
            printf("\n%s\n%s\n", palette_row_1, palette_row_2);
        }
        return;
    }

    size_t right_width = (size_t)terminal_width - left_width - 3;

    char separator[128];
    size_t host_len = user_host ? strlen(user_host) : 0;
    size_t sep_len = host_len < right_width ? host_len : right_width;
    if (sep_len > (sizeof(separator) - 1)) sep_len = sizeof(separator) - 1;
    for (size_t i = 0; i < sep_len; i++) separator[i] = '-';
    separator[sep_len] = '\0';

    char right_lines[MAX_RENDER_LINES][MAX_CAPTURE_LINE_LEN];
    size_t right_count = 0;

    if (user_host && user_host[0]) {
        append_wrapped_line(user_host, right_width, right_lines, &right_count, MAX_RENDER_LINES);
    }
    append_wrapped_line(separator, right_width, right_lines, &right_count, MAX_RENDER_LINES);

    for (size_t i = 0; i < g_info_line_count; i++) {
        append_wrapped_line(g_info_lines[i], right_width, right_lines, &right_count, MAX_RENDER_LINES);
    }

    if (color_enabled && right_width >= 16) {
        char palette_row_1[256];
        char palette_row_2[256];
        make_palette_row(0, palette_row_1, sizeof(palette_row_1));
        make_palette_row(8, palette_row_2, sizeof(palette_row_2));

        if (right_count < MAX_RENDER_LINES) {
            right_lines[right_count][0] = '\0';
            right_count++;
        }
        if (right_count < MAX_RENDER_LINES) {
            strncpy(right_lines[right_count], palette_row_1, MAX_CAPTURE_LINE_LEN - 1);
            right_lines[right_count][MAX_CAPTURE_LINE_LEN - 1] = '\0';
            right_count++;
        }
        if (right_count < MAX_RENDER_LINES) {
            strncpy(right_lines[right_count], palette_row_2, MAX_CAPTURE_LINE_LEN - 1);
            right_lines[right_count][MAX_CAPTURE_LINE_LEN - 1] = '\0';
            right_count++;
        }
    }

    size_t right_rows = right_count;
    size_t total_rows = scaled_logo_count > right_rows ? scaled_logo_count : right_rows;

    for (size_t row = 0; row < total_rows; row++) {
        size_t printed_left = 0;
        if (row < scaled_logo_count) {
            const char *logo_line = scaled_logo_lines[row];
            printed_left = utf8_display_width(logo_line);
            print_logo_line_inline(logo_line, color_enabled, use_truecolor, logo->r, logo->g, logo->b);
        }

        if (left_width > printed_left) {
            printf("%*s", (int)(left_width - printed_left), "");
        }
        printf("   ");

        if (row < right_count) {
            print_info_line_inline(right_lines[row], color_enabled, use_truecolor, logo->r, logo->g, logo->b);
        }

        printf("\n");
    }
}

void print_cat(const char* distro) {
    const struct DistroLogo *logo = find_logo_for_distro(distro);
    bool color_enabled = should_use_color();
    bool use_truecolor = terminal_supports_truecolor();
    int terminal_width = get_terminal_width();
    int terminal_height = get_terminal_height();
    size_t scale_num = 1;
    size_t scale_den = 1;

    if (terminal_width <= 0) terminal_width = 80;
    if (terminal_height <= 0) terminal_height = 24;

    choose_logo_scale(logo, terminal_width, terminal_height, false, true, &scale_num, &scale_den);

    char scaled_logo_storage[MAX_CAPTURE_LINES][MAX_CAPTURE_LINE_LEN];
    const char *scaled_logo_lines[MAX_CAPTURE_LINES];
    size_t scaled_logo_count = build_scaled_logo_lines(
        logo,
        scale_num,
        scale_den,
        scaled_logo_storage,
        scaled_logo_lines,
        MAX_CAPTURE_LINES
    );

    char fitted_line[MAX_CAPTURE_LINE_LEN];
    size_t width = (terminal_width > 0) ? (size_t)terminal_width : 80;
    for (size_t i = 0; i < scaled_logo_count; i++) {
        center_fit_for_width(scaled_logo_lines[i], width, fitted_line, sizeof(fitted_line));
        print_logo_line_inline(fitted_line, color_enabled, use_truecolor, logo->r, logo->g, logo->b);
        printf("\n");
    }
}
