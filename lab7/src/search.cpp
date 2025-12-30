#include "utf8.h"
#include "stem_ru.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

static void die(const char* msg) {
    std::fprintf(stderr, "ERROR: %s\n", msg);
    std::exit(1);
}

static void* xmalloc(size_t n) {
    void* p = std::malloc(n);
    if (!p) die("OOM");
    return p;
}
static void* xrealloc(void* p, size_t n) {
    void* q = std::realloc(p, n);
    if (!q) die("OOM");
    return q;
}

static bool read_all(const char* path, unsigned char** buf, size_t* n) {
    *buf = nullptr; *n = 0;
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
    long sz = std::ftell(f);
    if (sz < 0) { std::fclose(f); return false; }
    if (std::fseek(f, 0, SEEK_SET) != 0) { std::fclose(f); return false; }
    unsigned char* b = (unsigned char*)std::malloc((size_t)sz);
    if (!b) { std::fclose(f); return false; }
    size_t rd = std::fread(b, 1, (size_t)sz, f);
    std::fclose(f);
    if (rd != (size_t)sz) { std::free(b); return false; }
    *buf = b;
    *n = (size_t)sz;
    return true;
}

static uint32_t rd_u32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd_u64(const unsigned char* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

struct IndexView {
    const unsigned char* base;
    size_t bytes;

    uint32_t version;
    uint32_t flags;
    uint64_t docs_count;
    uint64_t terms_count;

    uint64_t dict_offset;
    uint64_t dict_bytes;
    uint64_t postings_offset;
    uint64_t postings_bytes;
    uint64_t docs_offset;
    uint64_t docs_bytes;

    uint64_t* dict_term_off;

    const unsigned char* docs_ptr;
    const unsigned char* docs_records_ptr;
    const unsigned char* docs_offs_ptr;
};

static bool load_index(const char* path, IndexView* iv) {
    unsigned char* buf = nullptr;
    size_t n = 0;
    if (!read_all(path, &buf, &n)) return false;

    if (n < 128) return false;
    if (std::memcmp(buf, "MAIIRIDX", 8) != 0) return false;

    iv->base = buf;
    iv->bytes = n;

    iv->version = rd_u32(buf + 8);
    iv->flags   = rd_u32(buf + 12);
    iv->docs_count  = rd_u64(buf + 16);
    iv->terms_count = rd_u64(buf + 24);
    iv->dict_offset     = rd_u64(buf + 32);
    iv->dict_bytes      = rd_u64(buf + 40);
    iv->postings_offset = rd_u64(buf + 48);
    iv->postings_bytes  = rd_u64(buf + 56);
    iv->docs_offset     = rd_u64(buf + 64);
    iv->docs_bytes      = rd_u64(buf + 72);

    if (iv->dict_offset + iv->dict_bytes > (uint64_t)n) return false;
    if (iv->postings_offset + iv->postings_bytes > (uint64_t)n) return false;
    if (iv->docs_offset + iv->docs_bytes > (uint64_t)n) return false;

    iv->dict_term_off = (uint64_t*)xmalloc((size_t)iv->terms_count * sizeof(uint64_t));

    uint64_t off = iv->dict_offset;
    for (uint64_t i = 0; i < iv->terms_count; ++i) {
        iv->dict_term_off[i] = off;
        if (off + 4 > (uint64_t)n) return false;
        uint32_t tl = rd_u32(buf + off);
        off += 4;
        off += (uint64_t)tl;
        off += 8;
        off += 4;
        off += 4;
        if (off > iv->dict_offset + iv->dict_bytes) return false;
    }

    iv->docs_ptr = buf + iv->docs_offset;
    if (iv->docs_bytes < 8) return false;

    iv->docs_offs_ptr = iv->docs_ptr + 8;
    iv->docs_records_ptr = iv->docs_offs_ptr + 8 * (size_t)iv->docs_count;

    return true;
}

static void free_index(IndexView* iv) {
    if (!iv) return;
    std::free((void*)iv->dict_term_off);
    std::free((void*)iv->base);
    std::memset(iv, 0, sizeof(*iv));
}

static int term_cmp_bytes(const unsigned char* a, size_t alen, const unsigned char* b, size_t blen) {
    size_t m = (alen < blen ? alen : blen);
    int c = std::memcmp(a, b, m);
    if (c != 0) return c;
    if (alen < blen) return -1;
    if (alen > blen) return 1;
    return 0;
}

static bool dict_find(const IndexView* iv, const unsigned char* term, size_t term_len,
                      uint64_t* out_post_off_rel, uint32_t* out_df) {
    int64_t lo = 0;
    int64_t hi = (int64_t)iv->terms_count - 1;

    while (lo <= hi) {
        int64_t mid = (lo + hi) / 2;
        uint64_t off = iv->dict_term_off[mid];

        uint32_t tl = rd_u32(iv->base + off);
        const unsigned char* tb = iv->base + off + 4;

        int c = term_cmp_bytes(term, term_len, tb, tl);
        if (c == 0) {
            uint64_t p_off = rd_u64(iv->base + off + 4 + tl);
            uint32_t df = rd_u32(iv->base + off + 4 + tl + 8);
            *out_post_off_rel = p_off;
            *out_df = df;
            return true;
        } else if (c < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return false;
}

static uint32_t* load_postings(const IndexView* iv, uint64_t post_off_rel, uint32_t df) {
    uint64_t abs = iv->postings_offset + post_off_rel;
    uint64_t need = abs + (uint64_t)df * 4ULL;
    if (need > (uint64_t)iv->bytes) return nullptr;
    uint32_t* a = (uint32_t*)xmalloc((size_t)df * sizeof(uint32_t));
    const unsigned char* p = iv->base + abs;
    for (uint32_t i = 0; i < df; ++i) {
        a[i] = rd_u32(p + 4ULL * i);
    }
    return a;
}

enum TokType {
    T_END = 0,
    T_TERM,
    T_AND,
    T_OR,
    T_NOT,
    T_LP,
    T_RP
};

struct Tok {
    TokType t;
    unsigned char* s;
    uint32_t len;
};

struct TokArr {
    Tok* a;
    size_t n;
    size_t cap;
};

static void ta_init(TokArr* x) { x->a = nullptr; x->n = 0; x->cap = 0; }
static void ta_push(TokArr* x, Tok v) {
    if (x->n == x->cap) {
        size_t nc = (x->cap == 0 ? 64 : x->cap * 2);
        x->a = (Tok*)xrealloc(x->a, nc * sizeof(Tok));
        x->cap = nc;
    }
    x->a[x->n++] = v;
}
static void ta_free(TokArr* x) {
    for (size_t i = 0; i < x->n; ++i) {
        if (x->a[i].t == T_TERM) std::free(x->a[i].s);
    }
    std::free(x->a);
    x->a = nullptr; x->n = 0; x->cap = 0;
}

static bool is_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool read_term(const unsigned char* q, size_t n, size_t* i, unsigned char** out_s, uint32_t* out_len) {
    size_t pos = *i;

    size_t cap = 64;
    size_t len = 0;
    unsigned char* tok = (unsigned char*)xmalloc(cap);

    bool any = false;

    while (pos < n) {
        size_t used = 0;
        uint32_t cp = 0;
        if (!utf8_decode_one(q + pos, n - pos, &used, &cp) || used == 0) break;
        if (!is_token_char(cp)) break;

        any = true;
        cp = to_lower_basic(cp);

        unsigned char enc[4];
        size_t el = utf8_encode_one(cp, enc);

        if (len + el > cap) {
            while (len + el > cap) cap *= 2;
            tok = (unsigned char*)xrealloc(tok, cap);
        }
        std::memcpy(tok + len, enc, el);
        len += el;
        pos += used;
    }

    if (!any) {
        std::free(tok);
        return false;
    }

    size_t L = len;
    stem_ru_utf8(tok, &L);
    len = L;

    unsigned char* out = (unsigned char*)xmalloc(len);
    std::memcpy(out, tok, len);
    std::free(tok);

    *out_s = out;
    *out_len = (uint32_t)len;
    *i = pos;
    return true;
}

static void tokenize_query(const unsigned char* q, size_t n, TokArr* out) {
    ta_init(out);
    size_t i = 0;

    TokType prev = T_END;

    while (i < n) {
        if (is_space(q[i])) { i++; continue; }

        if (q[i] == '(') {
            if (prev == T_TERM || prev == T_RP) ta_push(out, Tok{T_AND, nullptr, 0});
            ta_push(out, Tok{T_LP, nullptr, 0});
            prev = T_LP;
            i++;
            continue;
        }
        if (q[i] == ')') {
            ta_push(out, Tok{T_RP, nullptr, 0});
            prev = T_RP;
            i++;
            continue;
        }
        if (q[i] == '!') {
            if (prev == T_TERM || prev == T_RP) ta_push(out, Tok{T_AND, nullptr, 0});
            ta_push(out, Tok{T_NOT, nullptr, 0});
            prev = T_NOT;
            i++;
            continue;
        }
        if (q[i] == '&' && i + 1 < n && q[i+1] == '&') {
            ta_push(out, Tok{T_AND, nullptr, 0});
            prev = T_AND;
            i += 2;
            continue;
        }
        if (q[i] == '|' && i + 1 < n && q[i+1] == '|') {
            ta_push(out, Tok{T_OR, nullptr, 0});
            prev = T_OR;
            i += 2;
            continue;
        }

        if (prev == T_TERM || prev == T_RP) ta_push(out, Tok{T_AND, nullptr, 0});

        unsigned char* s = nullptr;
        uint32_t L = 0;
        size_t save = i;
        if (!read_term(q, n, &i, &s, &L)) {
            i = save + 1;
            continue;
        }

        ta_push(out, Tok{T_TERM, s, L});
        prev = T_TERM;
    }

    ta_push(out, Tok{T_END, nullptr, 0});
}

static int prec(TokType t) {
    if (t == T_NOT) return 3;
    if (t == T_AND) return 2;
    if (t == T_OR)  return 1;
    return 0;
}
static bool is_op(TokType t) { return t == T_NOT || t == T_AND || t == T_OR; }
static bool right_assoc(TokType t) { return t == T_NOT; }

struct TokStack {
    TokType* a;
    size_t n, cap;
};
static void ts_init(TokStack* s) { s->a = nullptr; s->n = 0; s->cap = 0; }
static void ts_push(TokStack* s, TokType t) {
    if (s->n == s->cap) {
        size_t nc = (s->cap == 0 ? 64 : s->cap * 2);
        s->a = (TokType*)xrealloc(s->a, nc * sizeof(TokType));
        s->cap = nc;
    }
    s->a[s->n++] = t;
}
static TokType ts_pop(TokStack* s) { return s->a[--s->n]; }
static TokType ts_top(TokStack* s) { return s->a[s->n - 1]; }
static void ts_free(TokStack* s) { std::free(s->a); s->a = nullptr; s->n = 0; s->cap = 0; }

static void to_rpn(const TokArr* in, TokArr* out) {
    ta_init(out);
    TokStack ops; ts_init(&ops);

    for (size_t i = 0; i < in->n; ++i) {
        Tok tk = in->a[i];
        if (tk.t == T_END) break;

        if (tk.t == T_TERM) {
            unsigned char* s = (unsigned char*)xmalloc(tk.len);
            std::memcpy(s, tk.s, tk.len);
            ta_push(out, Tok{T_TERM, s, tk.len});
            continue;
        }
        if (tk.t == T_LP) { ts_push(&ops, T_LP); continue; }
        if (tk.t == T_RP) {
            while (ops.n && ts_top(&ops) != T_LP) ta_push(out, Tok{ts_pop(&ops), nullptr, 0});
            if (ops.n && ts_top(&ops) == T_LP) ts_pop(&ops);
            continue;
        }
        if (is_op(tk.t)) {
            while (ops.n && is_op(ts_top(&ops))) {
                TokType top = ts_top(&ops);
                if ((right_assoc(tk.t) && prec(tk.t) < prec(top)) ||
                    (!right_assoc(tk.t) && prec(tk.t) <= prec(top))) {
                    ta_push(out, Tok{ts_pop(&ops), nullptr, 0});
                } else break;
            }
            ts_push(&ops, tk.t);
            continue;
        }
    }

    while (ops.n) {
        TokType t = ts_pop(&ops);
        if (t != T_LP) ta_push(out, Tok{t, nullptr, 0});
    }

    ta_push(out, Tok{T_END, nullptr, 0});
    ts_free(&ops);
}

struct List {
    uint32_t* a;
    uint32_t n;
};

static List list_from_term(const IndexView* iv, const unsigned char* term, uint32_t len) {
    uint64_t off = 0;
    uint32_t df = 0;
    if (!dict_find(iv, term, len, &off, &df) || df == 0) return List{nullptr, 0};
    uint32_t* p = load_postings(iv, off, df);
    return List{p, df};
}

static void list_free(List* x) {
    std::free(x->a);
    x->a = nullptr;
    x->n = 0;
}

static List op_and(const List& A, const List& B) {
    uint32_t i = 0, j = 0;
    uint32_t* out = (uint32_t*)xmalloc((size_t)(A.n < B.n ? A.n : B.n) * sizeof(uint32_t));
    uint32_t k = 0;
    while (i < A.n && j < B.n) {
        uint32_t a = A.a[i], b = B.a[j];
        if (a == b) { out[k++] = a; i++; j++; }
        else if (a < b) i++;
        else j++;
    }
    out = (uint32_t*)xrealloc(out, (size_t)k * sizeof(uint32_t));
    return List{out, k};
}

static List op_or(const List& A, const List& B) {
    uint32_t* out = (uint32_t*)xmalloc((size_t)(A.n + B.n) * sizeof(uint32_t));
    uint32_t i = 0, j = 0, k = 0;
    while (i < A.n && j < B.n) {
        uint32_t a = A.a[i], b = B.a[j];
        if (a == b) { out[k++] = a; i++; j++; }
        else if (a < b) { out[k++] = a; i++; }
        else { out[k++] = b; j++; }
    }
    while (i < A.n) out[k++] = A.a[i++];
    while (j < B.n) out[k++] = B.a[j++];
    out = (uint32_t*)xrealloc(out, (size_t)k * sizeof(uint32_t));
    return List{out, k};
}

static List op_not(const List& ALL, const List& A) {
    uint32_t* out = (uint32_t*)xmalloc((size_t)ALL.n * sizeof(uint32_t));
    uint32_t i = 0, j = 0, k = 0;
    while (i < ALL.n && j < A.n) {
        uint32_t x = ALL.a[i], y = A.a[j];
        if (x == y) { i++; j++; }
        else if (x < y) { out[k++] = x; i++; }
        else { j++; }
    }
    while (i < ALL.n) out[k++] = ALL.a[i++];
    out = (uint32_t*)xrealloc(out, (size_t)k * sizeof(uint32_t));
    return List{out, k};
}

struct ListStack {
    List* a;
    size_t n, cap;
};
static void ls_init(ListStack* s) { s->a = nullptr; s->n = 0; s->cap = 0; }
static void ls_push(ListStack* s, List v) {
    if (s->n == s->cap) {
        size_t nc = (s->cap == 0 ? 64 : s->cap * 2);
        s->a = (List*)xrealloc(s->a, nc * sizeof(List));
        s->cap = nc;
    }
    s->a[s->n++] = v;
}
static List ls_pop(ListStack* s) { return s->a[--s->n]; }
static void ls_free(ListStack* s) {
    for (size_t i = 0; i < s->n; ++i) list_free(&s->a[i]);
    std::free(s->a);
    s->a = nullptr; s->n = 0; s->cap = 0;
}

static List eval_rpn(const IndexView* iv, const TokArr* rpn, const List& ALL) {
    ListStack st; ls_init(&st);

    for (size_t i = 0; i < rpn->n; ++i) {
        Tok tk = rpn->a[i];
        if (tk.t == T_END) break;

        if (tk.t == T_TERM) {
            ls_push(&st, list_from_term(iv, tk.s, tk.len));
            continue;
        }
        if (tk.t == T_NOT) {
            if (st.n < 1) { ls_free(&st); return List{nullptr,0}; }
            List A = ls_pop(&st);
            List R = op_not(ALL, A);
            list_free(&A);
            ls_push(&st, R);
            continue;
        }
        if (tk.t == T_AND || tk.t == T_OR) {
            if (st.n < 2) { ls_free(&st); return List{nullptr,0}; }
            List B = ls_pop(&st);
            List A = ls_pop(&st);
            List R = (tk.t == T_AND) ? op_and(A, B) : op_or(A, B);
            list_free(&A);
            list_free(&B);
            ls_push(&st, R);
            continue;
        }
    }

    if (st.n != 1) { ls_free(&st); return List{nullptr,0}; }
    List out = ls_pop(&st);
    std::free(st.a);
    return out;
}

static const char* base_url_by_source(uint32_t source_id) {
    if (source_id == 1) return "https://ru.wikipedia.org/?curid=";
    if (source_id == 2) return "https://ru.wikisource.org/?curid=";
    return "https://ru.wikipedia.org/?curid=";
}

static bool get_doc_meta_v2(const IndexView* iv, uint32_t doc_id,
                            uint32_t* out_source_id, uint32_t* out_page_id,
                            const unsigned char** out_title, uint32_t* out_title_len) {
    if (doc_id == 0 || doc_id > (uint32_t)iv->docs_count) return false;
    const unsigned char* offs_p = iv->docs_offs_ptr + 8ULL * (uint64_t)(doc_id - 1);
    uint64_t rel = rd_u64(offs_p);
    const unsigned char* rec = iv->docs_records_ptr + rel;

    uint32_t rid = rd_u32(rec + 0);
    uint32_t sid = rd_u32(rec + 4);
    uint32_t pid = rd_u32(rec + 8);
    uint32_t tl  = rd_u32(rec + 12);
    const unsigned char* title = rec + 16;

    (void)rid;
    *out_source_id = sid;
    *out_page_id = pid;
    *out_title = title;
    *out_title_len = tl;
    return true;
}

static bool get_doc_meta_v1(const IndexView* iv, uint32_t doc_id,
                            uint32_t* out_page_id,
                            const unsigned char** out_title, uint32_t* out_title_len) {
    if (doc_id == 0 || doc_id > (uint32_t)iv->docs_count) return false;
    const unsigned char* offs_p = iv->docs_offs_ptr + 8ULL * (uint64_t)(doc_id - 1);
    uint64_t rel = rd_u64(offs_p);
    const unsigned char* rec = iv->docs_records_ptr + rel;

    uint32_t pid = rd_u32(rec + 4);
    uint32_t tl  = rd_u32(rec + 8);
    const unsigned char* title = rec + 12;

    *out_page_id = pid;
    *out_title = title;
    *out_title_len = tl;
    return true;
}

static void print_results(const IndexView* iv, const List& res, uint32_t limit, uint32_t offset) {
    uint32_t total = res.n;
    std::printf("OK\ttotal=%u\toffset=%u\tlimit=%u\n", total, offset, limit);

    uint32_t end = offset + limit;
    if (end > total) end = total;

    for (uint32_t i = offset; i < end; ++i) {
        uint32_t doc_id = res.a[i];

        uint32_t source_id = 1;
        uint32_t page_id = 0;
        const unsigned char* title = nullptr;
        uint32_t tl = 0;

        if (iv->version >= 2) {
            if (!get_doc_meta_v2(iv, doc_id, &source_id, &page_id, &title, &tl)) continue;
        } else {
            if (!get_doc_meta_v1(iv, doc_id, &page_id, &title, &tl)) continue;
            source_id = 1;
        }

        std::printf("%u\t%u\t", doc_id, page_id);
        std::fwrite(title, 1, tl, stdout);
        std::printf("\t%s%u\n", base_url_by_source(source_id), page_id);
    }
}

static bool read_line(FILE* f, unsigned char** out, size_t* out_n) {
    *out = nullptr; *out_n = 0;
    size_t cap = 256, n = 0;
    unsigned char* b = (unsigned char*)std::malloc(cap);
    if (!b) return false;

    int c;
    while ((c = std::fgetc(f)) != EOF) {
        if (c == '\r') continue;
        if (c == '\n') break;
        if (n + 1 > cap) {
            cap *= 2;
            b = (unsigned char*)std::realloc(b, cap);
            if (!b) return false;
        }
        b[n++] = (unsigned char)c;
    }
    if (c == EOF && n == 0) { std::free(b); return false; }
    *out = b;
    *out_n = n;
    return true;
}

static void usage() {
    std::fprintf(stderr,
        "Usage:\n"
        "  search.exe <index.bin> [--offset N] [--limit N] [--in queries.txt]\n"
    );
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }

    const char* index_path = argv[1];
    uint32_t offset = 0;
    uint32_t limit = 50;
    const char* in_path = nullptr;

    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--offset") == 0 && i + 1 < argc) {
            offset = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
            limit = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--in") == 0 && i + 1 < argc) {
            in_path = argv[++i];
        }
    }

    FILE* fin = stdin;
    if (in_path) {
        fin = std::fopen(in_path, "rb");
        if (!fin) die("cannot open --in file");
    }

    IndexView iv{};
    if (!load_index(index_path, &iv)) die("load_index failed");

    std::fprintf(stderr, "[index] version=%u docs=%I64u terms=%I64u\n",
        iv.version,
        (unsigned long long)iv.docs_count,
        (unsigned long long)iv.terms_count);

    List ALL;
    ALL.n = (uint32_t)iv.docs_count;
    ALL.a = (uint32_t*)xmalloc((size_t)ALL.n * sizeof(uint32_t));
    for (uint32_t i = 0; i < ALL.n; ++i) ALL.a[i] = i + 1;

    unsigned char* line = nullptr;
    size_t ln = 0;

    while (read_line(fin, &line, &ln)) {
        bool any = false;
        for (size_t k = 0; k < ln; ++k) if (!is_space(line[k])) { any = true; break; }
        if (!any) { std::free(line); continue; }

        TokArr toks, rpn;
        auto t0 = std::chrono::high_resolution_clock::now();
        tokenize_query(line, ln, &toks);
        to_rpn(&toks, &rpn);
        List res = eval_rpn(&iv, &rpn, ALL);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
        std::fprintf(stderr, "[time] %.3f ms\n", ms);

        print_results(&iv, res, limit, offset);

        list_free(&res);
        ta_free(&toks);
        ta_free(&rpn);
        std::free(line);
        line = nullptr;
        ln = 0;
    }

    if (fin != stdin) std::fclose(fin);
    list_free(&ALL);
    free_index(&iv);
    return 0;
}
