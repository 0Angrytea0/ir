#include "tokenize.h"
#include "utf8.h"
#include "stem_ru.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool read_all(const char* path, unsigned char** buf, size_t* n) {
    *buf = nullptr;
    *n = 0;

    FILE* f = std::fopen(path, "rb");
    if (!f) return false;

    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
    long sz = std::ftell(f);
    if (sz < 0) { std::fclose(f); return false; }
    if (std::fseek(f, 0, SEEK_SET) != 0) { std::fclose(f); return false; }

    unsigned char* b = (unsigned char*)std::malloc((size_t)sz + 1);
    if (!b) { std::fclose(f); return false; }

    size_t rd = std::fread(b, 1, (size_t)sz, f);
    std::fclose(f);

    if (rd != (size_t)sz) { std::free(b); return false; }
    b[sz] = 0;
    *buf = b;
    *n = (size_t)sz;
    return true;
}

static bool write_token(FILE* out, const unsigned char* tok, size_t len) {
    if (!out || !tok || len == 0) return true;
    if (std::fwrite(tok, 1, len, out) != len) return false;
    if (std::fputc('\n', out) == EOF) return false;
    return true;
}

static bool flush_token(FILE* out, unsigned char* tok, size_t* tok_len,
                        unsigned long long* tok_chars, TokenizeStats* st, bool do_stem) {
    if (*tok_len == 0) return true;

    size_t L = *tok_len;
    if (do_stem) stem_ru_utf8(tok, &L);

    if (!write_token(out, tok, L)) return false;

    if (st) {
        st->tokens_out++;
        st->token_chars_sum += *tok_chars;
    }

    *tok_len = 0;
    *tok_chars = 0;
    return true;
}

bool tokenize_file_to_stream_ex(const char* input_path, FILE* out, TokenizeStats* st, bool do_stem) {
    if (st) { st->bytes_in = 0; st->tokens_out = 0; st->token_chars_sum = 0; }
    if (!input_path || !out) return false;

    unsigned char* buf = nullptr;
    size_t n = 0;
    if (!read_all(input_path, &buf, &n)) return false;
    if (st) st->bytes_in = (unsigned long long)n;

    unsigned char* tok = nullptr;
    size_t tok_cap = 0;
    size_t tok_len = 0;
    unsigned long long tok_chars = 0;

    auto tok_reserve = [&](size_t need) -> bool {
        if (need <= tok_cap) return true;
        size_t new_cap = (tok_cap == 0 ? 64 : tok_cap);
        while (new_cap < need) new_cap *= 2;
        unsigned char* p = (unsigned char*)std::realloc(tok, new_cap);
        if (!p) return false;
        tok = p;
        tok_cap = new_cap;
        return true;
    };

    size_t i = 0;
    while (i < n) {
        size_t used = 0;
        uint32_t cp = 0;
        bool ok = utf8_decode_one(buf + i, n - i, &used, &cp);
        if (!ok || used == 0) {
            if (!flush_token(out, tok, &tok_len, &tok_chars, st, do_stem)) { std::free(tok); std::free(buf); return false; }
            i += 1;
            continue;
        }

        if (is_token_char(cp)) {
            cp = to_lower_basic(cp);
            unsigned char enc[4];
            size_t enc_len = utf8_encode_one(cp, enc);
            if (!tok_reserve(tok_len + enc_len)) { std::free(tok); std::free(buf); return false; }
            std::memcpy(tok + tok_len, enc, enc_len);
            tok_len += enc_len;
            tok_chars += 1;
        } else {
            if (!flush_token(out, tok, &tok_len, &tok_chars, st, do_stem)) { std::free(tok); std::free(buf); return false; }
        }

        i += used;
    }

    if (!flush_token(out, tok, &tok_len, &tok_chars, st, do_stem)) { std::free(tok); std::free(buf); return false; }

    std::free(tok);
    std::free(buf);
    return true;
}
