#include "utf8.h"

bool utf8_decode_one(const unsigned char* s, size_t n, size_t* used, uint32_t* cp) {
    if (!s || n == 0) return false;

    unsigned char b0 = s[0];
    if (b0 < 0x80) {
        *cp = b0;
        *used = 1;
        return true;
    }

    if ((b0 & 0xE0) == 0xC0) {
        if (n < 2) return false;
        unsigned char b1 = s[1];
        if ((b1 & 0xC0) != 0x80) return false;
        uint32_t v = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
        if (v < 0x80) return false; 
        *cp = v;
        *used = 2;
        return true;
    }

    // 3-byte
    if ((b0 & 0xF0) == 0xE0) {
        if (n < 3) return false;
        unsigned char b1 = s[1], b2 = s[2];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return false;
        uint32_t v = ((uint32_t)(b0 & 0x0F) << 12) |
                     ((uint32_t)(b1 & 0x3F) << 6) |
                     (uint32_t)(b2 & 0x3F);
        if (v < 0x800) return false;
        *cp = v;
        *used = 3;
        return true;
    }

    if ((b0 & 0xF8) == 0xF0) {
        if (n < 4) return false;
        unsigned char b1 = s[1], b2 = s[2], b3 = s[3];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return false;
        uint32_t v = ((uint32_t)(b0 & 0x07) << 18) |
                     ((uint32_t)(b1 & 0x3F) << 12) |
                     ((uint32_t)(b2 & 0x3F) << 6) |
                     (uint32_t)(b3 & 0x3F);
        if (v < 0x10000 || v > 0x10FFFF) return false; 
        *cp = v;
        *used = 4;
        return true;
    }

    return false;
}

size_t utf8_encode_one(uint32_t cp, unsigned char out[4]) {
    if (cp <= 0x7F) {
        out[0] = (unsigned char)cp;
        return 1;
    }
    if (cp <= 0x7FF) {
        out[0] = (unsigned char)(0xC0 | ((cp >> 6) & 0x1F));
        out[1] = (unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp <= 0xFFFF) {
        out[0] = (unsigned char)(0xE0 | ((cp >> 12) & 0x0F));
        out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (unsigned char)(0xF0 | ((cp >> 18) & 0x07));
    out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

static bool is_latin_letter(uint32_t cp) {
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}

static bool is_digit(uint32_t cp) {
    return (cp >= '0' && cp <= '9');
}

static bool is_cyrillic_letter(uint32_t cp) {
    if (cp == 0x0401 || cp == 0x0451) return true;    
    if (cp >= 0x0410 && cp <= 0x044F) return true;     
    return false;
}

bool is_token_char(uint32_t cp) {
    return is_digit(cp) || is_latin_letter(cp) || is_cyrillic_letter(cp);
}

uint32_t to_lower_basic(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;

    if (cp == 0x0401) return 0x0451;
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 32; 

    return cp;
}
