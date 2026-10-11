#include "../libs/cupidconf.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

/* Independent dynamic-programming reference for the exhaustive small cases. */
static bool reference(const char *pattern, const char *text) {
    bool dp[6][6] = {{true}};
    size_t m = strlen(pattern), n = strlen(text);
    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 0; j <= n; ++j) {
            if (pattern[i - 1] == '*')
                dp[i][j] = dp[i - 1][j] || (j && dp[i][j - 1]);
            else if (j)
                dp[i][j] = dp[i - 1][j - 1] &&
                    (pattern[i - 1] == '?' || pattern[i - 1] == text[j - 1]);
        }
    }
    return dp[m][n];
}
static unsigned power(unsigned base, unsigned n) {
    unsigned result = 1;
    while (n--) result *= base;
    return result;
}
static void word(char *out, unsigned code, unsigned length, const char *alphabet) {
    unsigned base = (unsigned)strlen(alphabet);
    for (unsigned i = 0; i < length; ++i) { out[i] = alphabet[code % base]; code /= base; }
    out[length] = 0;
}
int main(void) {
    char pattern[64], key[64];
    cupidconf_entry entry = {key, "first", NULL};
    cupidconf_t config = {&entry};
    for (unsigned m = 0; m <= 5; ++m) for (unsigned p = 0; p < power(4, m); ++p) {
        word(pattern, p, m, "ab*?");
        for (unsigned n = 0; n <= 5; ++n) for (unsigned k = 0; k < power(2, n); ++k) {
            word(key, k, n, "ab");
            CHECK((cupidconf_get(&config, pattern) != NULL) == reference(pattern, key));
        }
    }
    memset(key, 'a', 60); key[60] = 0;
    for (int i = 0; i < 30; ++i) { pattern[2*i] = '*'; pattern[2*i+1] = 'a'; }
    pattern[60] = 'b'; pattern[61] = 0;
    CHECK(cupidconf_get(&config, pattern) == NULL);
    key[59] = 'b';
    CHECK(cupidconf_get(&config, pattern) == entry.value);
    cupidconf_entry second = {"ab", "second", NULL};
    entry.next = &second;
    int count = 0;
    char **values = cupidconf_get_list(&config, "a*b", &count);
    CHECK(values && count == 2 && values[0] == entry.value && values[1] == second.value);
    free(values);
    CHECK(cupidconf_get(NULL, "*") == NULL);
    CHECK(cupidconf_get(&config, NULL) == NULL);
    puts("Wildcard regression tests passed");
    return 0;
}
