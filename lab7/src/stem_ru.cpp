#include "stem_ru.h"
#include <cstring>

static bool has_digit_ascii(const unsigned char* s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (s[i] >= '0' && s[i] <= '9') return true;
    }
    return false;
}
static bool looks_cyrillic_utf8(const unsigned char* s, size_t n) {
    for (size_t i = 0; i + 1 < n; ++i) {
        if (s[i] == 0xD0 || s[i] == 0xD1) return true;
    }
    return false;
}

static bool ends_with(const unsigned char* s, size_t n, const char* suf) {
    size_t m = std::strlen(suf);             
    if (m == 0 || m > n) return false;
    return std::memcmp(s + (n - m), suf, m) == 0;
}

static const size_t MIN_STEM_BYTES = 6;

void stem_ru_utf8(unsigned char* tok, size_t* len) {
    if (!tok || !len) return;
    size_t n = *len;
    if (n < MIN_STEM_BYTES) return;

    if (has_digit_ascii(tok, n)) return;

    if (!looks_cyrillic_utf8(tok, n)) return;

    if (n >= 4) {
        const unsigned char* end = tok + (n - 4);
        if (end[0] == 0xD1 && end[1] == 0x81 && end[2] == 0xD1 && (end[3] == 0x8F || end[3] == 0x8C)) {
            if (n - 4 >= MIN_STEM_BYTES) {
                n -= 4;
            }
        }
    }

    static const char* suffixes[] = {
        "иями", "ями", "ами",
        "ыми", "ими",
        "ого", "его",
        "ому", "ему",
        "ыми", "ими",
        "ых", "их",
        "ах", "ях",
        "ов", "ев",
        "ом", "ем",
        "ам", "ям",
        "ую", "юю",
        "ая", "яя",
        "ое", "ее",
        "ый", "ий",
        "ые", "ие",
        "а", "я", "о", "е", "ы", "и", "у", "ю"
    };

    for (size_t k = 0; k < sizeof(suffixes)/sizeof(suffixes[0]); ++k) {
        const char* suf = suffixes[k];
        size_t m = std::strlen(suf);
        if (m == 0 || m > n) continue;
        if (ends_with(tok, n, suf)) {
            if (n - m >= MIN_STEM_BYTES) {
                n -= m;
            }
            break; 
        }
    }

    if (n >= 2) {
        unsigned char b0 = tok[n - 2];
        unsigned char b1 = tok[n - 1];
        if (b0 == 0xD1 && (b1 == 0x8C || b1 == 0x8A)) {
            if (n - 2 >= MIN_STEM_BYTES) {
                n -= 2;
            }
        }
    }

    *len = n;
}
