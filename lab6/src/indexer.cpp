#include "win_files.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void die(const char* msg) {
    std::fprintf(stderr, "ERROR: %s\n", msg);
    std::exit(1);
}

static uint64_t now_qpc() {
    LARGE_INTEGER x;
    QueryPerformanceCounter(&x);
    return (uint64_t)x.QuadPart;
}
static double qpc_seconds(uint64_t t0, uint64_t t1) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (double)(t1 - t0) / (double)f.QuadPart;
}

static uint64_t fnv1a64(const unsigned char* s, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i) {
        h ^= (uint64_t)s[i];
        h *= 1099511628211ULL;
    }
    if (h == 0) h = 1;
    return h;
}

struct BytePool {
    unsigned char* buf;
    size_t len;
    size_t cap;
};

static void pool_init(BytePool* p) { p->buf = nullptr; p->len = 0; p->cap = 0; }

static bool pool_reserve(BytePool* p, size_t need) {
    if (need <= p->cap) return true;
    size_t nc = (p->cap == 0 ? (1u << 20) : p->cap);
    while (nc < need) nc *= 2;
    unsigned char* nb = (unsigned char*)std::realloc(p->buf, nc);
    if (!nb) return false;
    p->buf = nb;
    p->cap = nc;
    return true;
}

static uint32_t pool_add(BytePool* p, const unsigned char* s, size_t n) {
    size_t need = p->len + n;
    if (!pool_reserve(p, need)) return UINT32_MAX;
    uint32_t off = (uint32_t)p->len;
    std::memcpy(p->buf + p->len, s, n);
    p->len += n;
    return off;
}

static const uint32_t POST_BLOCK = 32;

struct PostBlock {
    uint32_t used;
    uint32_t doc[POST_BLOCK];
    PostBlock* next;
};

struct TermEntry {
    uint64_t hash;
    uint32_t off;
    uint32_t len;
    uint32_t term_id;
    uint8_t used;
    PostBlock* first;
    PostBlock* last;
    uint32_t df;
};

struct TermDict {
    TermEntry* tab;
    size_t cap;
    size_t size;
    BytePool pool;
};

static bool term_equals(const TermDict* d, const TermEntry* e, const unsigned char* s, size_t n) {
    if (e->len != (uint32_t)n) return false;
    return std::memcmp(d->pool.buf + e->off, s, n) == 0;
}

static bool dict_init(TermDict* d, size_t cap) {
    d->tab = (TermEntry*)std::calloc(cap, sizeof(TermEntry));
    if (!d->tab) return false;
    d->cap = cap;
    d->size = 0;
    pool_init(&d->pool);
    return true;
}

static bool dict_rehash(TermDict* d, size_t new_cap) {
    TermEntry* old = d->tab;
    size_t old_cap = d->cap;

    TermEntry* nt = (TermEntry*)std::calloc(new_cap, sizeof(TermEntry));
    if (!nt) return false;

    d->tab = nt;
    d->cap = new_cap;
    d->size = 0;

    size_t mask = d->cap - 1;
    for (size_t i = 0; i < old_cap; ++i) {
        if (!old[i].used) continue;
        size_t pos = (size_t)old[i].hash & mask;
        while (d->tab[pos].used) pos = (pos + 1) & mask;
        d->tab[pos] = old[i];
        d->size++;
    }

    std::free(old);
    return true;
}

static bool dict_get_or_add(TermDict* d, const unsigned char* s, size_t n, uint32_t* out_term_id) {
    if ((d->size + 1) * 10 >= d->cap * 7) {
        if (!dict_rehash(d, d->cap * 2)) return false;
    }

    uint64_t h = fnv1a64(s, n);
    size_t mask = d->cap - 1;
    size_t pos = (size_t)h & mask;

    while (d->tab[pos].used) {
        TermEntry* e = &d->tab[pos];
        if (e->hash == h && term_equals(d, e, s, n)) {
            *out_term_id = e->term_id;
            return true;
        }
        pos = (pos + 1) & mask;
    }

    uint32_t off = pool_add(&d->pool, s, n);
    if (off == UINT32_MAX) return false;

    TermEntry* ne = &d->tab[pos];
    ne->used = 1;
    ne->hash = h;
    ne->off = off;
    ne->len = (uint32_t)n;
    ne->term_id = (uint32_t)d->size;
    ne->first = ne->last = nullptr;
    ne->df = 0;

    d->size++;
    *out_term_id = ne->term_id;
    return true;
}

static bool postings_append(TermEntry* e, uint32_t doc_id) {
    if (!e->last || e->last->used == POST_BLOCK) {
        PostBlock* b = (PostBlock*)std::calloc(1, sizeof(PostBlock));
        if (!b) return false;
        b->used = 0;
        b->next = nullptr;
        if (!e->first) e->first = b;
        if (e->last) e->last->next = b;
        e->last = b;
    }
    e->last->doc[e->last->used++] = doc_id;
    e->df += 1;
    return true;
}

struct DocSet {
    uint32_t* tab;
    size_t cap;
    size_t size;
};

static uint32_t mix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

static void docset_init(DocSet* s, size_t cap) {
    s->cap = 1;
    while (s->cap < cap) s->cap <<= 1;
    s->tab = (uint32_t*)std::malloc(s->cap * sizeof(uint32_t));
    if (!s->tab) die("docset malloc");
    for (size_t i = 0; i < s->cap; ++i) s->tab[i] = 0xFFFFFFFFu;
    s->size = 0;
}

static bool docset_rehash(DocSet* s, size_t new_cap) {
    uint32_t* old = s->tab;
    size_t old_cap = s->cap;

    s->cap = 1;
    while (s->cap < new_cap) s->cap <<= 1;
    s->tab = (uint32_t*)std::malloc(s->cap * sizeof(uint32_t));
    if (!s->tab) return false;
    for (size_t i = 0; i < s->cap; ++i) s->tab[i] = 0xFFFFFFFFu;

    size_t mask = s->cap - 1;
    for (size_t i = 0; i < old_cap; ++i) {
        uint32_t v = old[i];
        if (v == 0xFFFFFFFFu) continue;
        size_t pos = (size_t)mix32(v) & mask;
        while (s->tab[pos] != 0xFFFFFFFFu) pos = (pos + 1) & mask;
        s->tab[pos] = v;
    }

    std::free(old);
    return true;
}

static bool docset_add(DocSet* s, uint32_t term_id, bool* inserted_new) {
    if ((s->size + 1) * 10 >= s->cap * 7) {
        if (!docset_rehash(s, s->cap * 2)) return false;
    }
    size_t mask = s->cap - 1;
    size_t pos = (size_t)mix32(term_id) & mask;

    while (true) {
        uint32_t cur = s->tab[pos];
        if (cur == 0xFFFFFFFFu) {
            s->tab[pos] = term_id;
            s->size++;
            *inserted_new = true;
            return true;
        }
        if (cur == term_id) {
            *inserted_new = false;
            return true;
        }
        pos = (pos + 1) & mask;
    }
}

static void docset_free(DocSet* s) {
    std::free(s->tab);
    s->tab = nullptr;
    s->cap = 0;
    s->size = 0;
}

static bool read_all(const char* path, unsigned char** buf, size_t* n) {
    *buf = nullptr; *n = 0;
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

static uint32_t parse_doc_id_from_name(const char* name) {
    uint32_t v = 0;
    for (int i = 0; i < 8 && name[i]; ++i) {
        char c = name[i];
        if (c < '0' || c > '9') break;
        v = v * 10u + (uint32_t)(c - '0');
    }
    return v;
}

struct FileItem {
    uint32_t doc_id;
    char* full;
    char* name;
};

struct FileList {
    FileItem* a;
    size_t n;
    size_t cap;
};

static void fl_init(FileList* fl) { fl->a = nullptr; fl->n = 0; fl->cap = 0; }

static bool fl_push(FileList* fl, uint32_t doc_id, const char* full, const char* name) {
    if (fl->n == fl->cap) {
        size_t nc = (fl->cap == 0 ? 1024 : fl->cap * 2);
        FileItem* na = (FileItem*)std::realloc(fl->a, nc * sizeof(FileItem));
        if (!na) return false;
        fl->a = na;
        fl->cap = nc;
    }
    size_t lf = std::strlen(full), ln = std::strlen(name);
    char* cf = (char*)std::malloc(lf + 1);
    char* cn = (char*)std::malloc(ln + 1);
    if (!cf || !cn) return false;
    std::memcpy(cf, full, lf + 1);
    std::memcpy(cn, name, ln + 1);

    fl->a[fl->n++] = {doc_id, cf, cn};
    return true;
}

static void fl_free(FileList* fl) {
    for (size_t i = 0; i < fl->n; ++i) {
        std::free(fl->a[i].full);
        std::free(fl->a[i].name);
    }
    std::free(fl->a);
    fl->a = nullptr; fl->n = 0; fl->cap = 0;
}

static void fl_qsort(FileItem* a, int l, int r) {
    while (l < r) {
        uint32_t pivot = a[(l + r) / 2].doc_id;
        int i = l, j = r;
        while (i <= j) {
            while (a[i].doc_id < pivot) i++;
            while (a[j].doc_id > pivot) j--;
            if (i <= j) {
                FileItem tmp = a[i]; a[i] = a[j]; a[j] = tmp;
                i++; j--;
            }
        }
        if (j - l < r - i) {
            if (l < j) fl_qsort(a, l, j);
            l = i;
        } else {
            if (i < r) fl_qsort(a, i, r);
            r = j;
        }
    }
}

struct LocalMeta {
    uint32_t page_id;
    uint32_t title_off;
    uint32_t title_len;
    uint32_t source_id;
};

static uint32_t source_id_from_name(const char* s) {
    if (!s) return 0;
    if (std::strcmp(s, "ruwiki") == 0) return 1;
    if (std::strcmp(s, "ru_wikisource") == 0) return 2;
    return 3;
}

static bool split_tsv_4(const char* line, char* a, char* b, char* c, char* d, size_t cap) {
    a[0]=b[0]=c[0]=d[0]=0;
    const char* p = line;
    const char* t1 = std::strchr(p, '\t'); if (!t1) return false;
    const char* t2 = std::strchr(t1+1, '\t'); if (!t2) return false;
    const char* t3 = std::strchr(t2+1, '\t'); if (!t3) return false;

    size_t la = (size_t)(t1 - p);
    size_t lb = (size_t)(t2 - (t1+1));
    size_t lc = (size_t)(t3 - (t2+1));
    size_t ld = std::strlen(t3+1);

    while (ld > 0 && (t3[1 + (ld-1)]=='\n' || t3[1 + (ld-1)]=='\r')) ld--;

    if (la>=cap || lb>=cap || lc>=cap || ld>=cap) return false;
    std::memcpy(a, p, la); a[la]=0;
    std::memcpy(b, t1+1, lb); b[lb]=0;
    std::memcpy(c, t2+1, lc); c[lc]=0;
    std::memcpy(d, t3+1, ld); d[ld]=0;
    return true;
}

static bool read_meta_any(const char* path, LocalMeta** out_meta, uint32_t* out_max_id, BytePool* title_pool) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;

    char line[16384];
    if (!std::fgets(line, sizeof(line), f)) { std::fclose(f); return false; }

    long pos0 = std::ftell(f);
    uint32_t max_id = 0;

    while (std::fgets(line, sizeof(line), f)) {
        char a[256], b[256], c[8192], d[256];
        if (!split_tsv_4(line, a, b, c, d, sizeof(a))) continue;
        uint32_t doc_id = (uint32_t)std::strtoul(a, nullptr, 10);
        if (doc_id > max_id) max_id = doc_id;
    }

    LocalMeta* meta = (LocalMeta*)std::calloc(max_id + 1, sizeof(LocalMeta));
    if (!meta) { std::fclose(f); return false; }

    std::fseek(f, pos0, SEEK_SET);

    while (std::fgets(line, sizeof(line), f)) {
        char a[256], b[256], c[8192], d[256];
        if (!split_tsv_4(line, a, b, c, d, sizeof(a))) continue;

        uint32_t doc_id = (uint32_t)std::strtoul(a, nullptr, 10);
        uint32_t page_id = (uint32_t)std::strtoul(b, nullptr, 10);

        size_t tl = std::strlen(c);
        uint32_t off = pool_add(title_pool, (const unsigned char*)c, tl);
        if (off == UINT32_MAX) { std::free(meta); std::fclose(f); return false; }

        meta[doc_id].page_id = page_id;
        meta[doc_id].title_off = off;
        meta[doc_id].title_len = (uint32_t)tl;
        meta[doc_id].source_id = source_id_from_name(d);
    }

    std::fclose(f);
    *out_meta = meta;
    *out_max_id = max_id;
    return true;
}

static TermEntry** build_entries_by_id(TermDict* d) {
    TermEntry** by = (TermEntry**)std::malloc(d->size * sizeof(TermEntry*));
    if (!by) return nullptr;
    for (size_t i = 0; i < d->cap; ++i) {
        if (d->tab[i].used) by[d->tab[i].term_id] = &d->tab[i];
    }
    return by;
}

static int term_cmp(const TermDict* d, const TermEntry* a, const TermEntry* b) {
    const unsigned char* sa = d->pool.buf + a->off;
    const unsigned char* sb = d->pool.buf + b->off;
    size_t na = a->len, nb = b->len;
    size_t m = (na < nb ? na : nb);
    int c = std::memcmp(sa, sb, m);
    if (c != 0) return c;
    if (na < nb) return -1;
    if (na > nb) return 1;
    return 0;
}

static void term_qsort(uint32_t* ids, int l, int r, const TermDict* d, TermEntry** by_id) {
    while (l < r) {
        uint32_t pivot_id = ids[(l + r) / 2];
        TermEntry* pivot = by_id[pivot_id];
        int i = l, j = r;
        while (i <= j) {
            while (term_cmp(d, by_id[ids[i]], pivot) < 0) i++;
            while (term_cmp(d, by_id[ids[j]], pivot) > 0) j--;
            if (i <= j) {
                uint32_t tmp = ids[i]; ids[i] = ids[j]; ids[j] = tmp;
                i++; j--;
            }
        }
        if (j - l < r - i) {
            if (l < j) term_qsort(ids, l, j, d, by_id);
            l = i;
        } else {
            if (i < r) term_qsort(ids, i, r, d, by_id);
            r = j;
        }
    }
}

static void wr_u32(FILE* f, uint32_t v) { std::fwrite(&v, 1, 4, f); }
static void wr_u64(FILE* f, uint64_t v) { std::fwrite(&v, 1, 8, f); }

struct EnumCtx { FileList* fl; };

static void on_tok(const char* full_path, const char* file_name, void* user) {
    EnumCtx* ec = (EnumCtx*)user;
    uint32_t doc_id = parse_doc_id_from_name(file_name);
    if (doc_id == 0) return;
    if (!fl_push(ec->fl, doc_id, full_path, file_name)) die("fl_push OOM");
}

static void usage() {
    std::fprintf(stderr,
        "Usage:\n"
        "  indexer.exe --add <tok_dir> <meta_tsv> --add <tok_dir> <meta_tsv> <out_index_bin>\n"
    );
}

struct DocRec {
    uint32_t source_id;
    uint32_t page_id;
    uint32_t title_off;
    uint32_t title_len;
};

int main(int argc, char** argv) {
    if (argc < 5) { usage(); return 2; }

    const char* out_bin = argv[argc - 1];

    uint64_t t0 = now_qpc();

    TermDict dict;
    if (!dict_init(&dict, 1 << 20)) die("dict_init OOM");

    TermEntry** by_id = nullptr;

    BytePool title_pool; pool_init(&title_pool);

    DocRec* docs = nullptr;
    uint32_t docs_cap = 0;
    uint32_t docs_count = 0;

    uint64_t total_token_bytes = 0;
    uint64_t total_token_count = 0;
    uint64_t total_input_bytes = 0;

    uint64_t t_scan0 = now_qpc();

    int i = 1;
    while (i < argc - 1) {
        if (std::strcmp(argv[i], "--add") != 0) die("expected --add");
        if (i + 2 >= argc - 1) die("bad --add args");
        const char* tok_dir = argv[i + 1];
        const char* meta_tsv = argv[i + 2];
        i += 3;

        LocalMeta* meta = nullptr;
        uint32_t meta_max = 0;
        if (!read_meta_any(meta_tsv, &meta, &meta_max, &title_pool)) die("read_meta_tsv failed");

        FileList fl; fl_init(&fl);
        EnumCtx ec; ec.fl = &fl;
        if (!list_tok_files(tok_dir, on_tok, &ec)) die("list_tok_files failed");
        if (fl.n == 0) die("no .tok files found");
        fl_qsort(fl.a, 0, (int)fl.n - 1);

        for (size_t fi = 0; fi < fl.n; ++fi) {
            uint32_t local_doc_id = fl.a[fi].doc_id;
            if (local_doc_id == 0 || local_doc_id > meta_max) continue;
            if (meta[local_doc_id].title_len == 0) continue;

            unsigned char* buf = nullptr;
            size_t n = 0;
            if (!read_all(fl.a[fi].full, &buf, &n)) die("read tok failed");
            total_input_bytes += (uint64_t)n;

            DocSet ds; docset_init(&ds, 4096);

            size_t pos = 0, start = 0;
            while (pos <= n) {
                if (pos == n || buf[pos] == '\n') {
                    size_t len = (pos > start ? (pos - start) : 0);
                    if (len > 0 && buf[start + len - 1] == '\r') len--;
                    if (len > 0) {
                        total_token_bytes += (uint64_t)len;
                        total_token_count++;

                        uint32_t term_id;
                        if (!dict_get_or_add(&dict, buf + start, len, &term_id)) die("dict_get_or_add OOM");

                        bool inserted;
                        if (!docset_add(&ds, term_id, &inserted)) die("docset_add OOM");
                    }
                    pos++; start = pos;
                } else pos++;
            }

            if (by_id) std::free(by_id);
            by_id = build_entries_by_id(&dict);
            if (!by_id) die("build_entries_by_id OOM");

            uint32_t global_doc_id = docs_count + 1;

            for (size_t k = 0; k < ds.cap; ++k) {
                uint32_t tid = ds.tab[k];
                if (tid == 0xFFFFFFFFu) continue;
                TermEntry* e2 = by_id[tid];
                if (!postings_append(e2, global_doc_id)) die("postings_append OOM");
            }

            docset_free(&ds);
            std::free(buf);

            if (docs_count + 1 > docs_cap) {
                uint32_t nc = (docs_cap == 0 ? 8192 : docs_cap * 2);
                DocRec* nd = (DocRec*)std::realloc(docs, (size_t)nc * sizeof(DocRec));
                if (!nd) die("docs realloc OOM");
                docs = nd;
                docs_cap = nc;
            }

            docs[docs_count].source_id = meta[local_doc_id].source_id;
            docs[docs_count].page_id = meta[local_doc_id].page_id;
            docs[docs_count].title_off = meta[local_doc_id].title_off;
            docs[docs_count].title_len = meta[local_doc_id].title_len;
            docs_count++;

            if ((docs_count % 1000u) == 0u) {
                std::fprintf(stderr, "[prog] docs=%u terms=%I64u\n", docs_count, (unsigned long long)dict.size);
            }
        }

        fl_free(&fl);
        std::free(meta);
    }

    uint64_t t_scan1 = now_qpc();

    if (by_id) std::free(by_id);
    by_id = build_entries_by_id(&dict);
    if (!by_id) die("build_entries_by_id OOM final");

    uint32_t terms_count = (uint32_t)dict.size;

    uint32_t* term_ids = (uint32_t*)std::malloc((size_t)terms_count * sizeof(uint32_t));
    if (!term_ids) die("term_ids OOM");
    for (uint32_t k = 0; k < terms_count; ++k) term_ids[k] = k;
    term_qsort(term_ids, 0, (int)terms_count - 1, &dict, by_id);

    uint64_t* postings_off = (uint64_t*)std::malloc((size_t)terms_count * sizeof(uint64_t));
    if (!postings_off) die("postings_off OOM");
    uint64_t cur = 0;
    for (uint32_t si = 0; si < terms_count; ++si) {
        TermEntry* e = by_id[term_ids[si]];
        postings_off[si] = cur;
        cur += (uint64_t)e->df * 4ULL;
    }
    uint64_t postings_bytes = cur;

    uint64_t sum_term_bytes = 0;
    for (uint32_t k = 0; k < terms_count; ++k) sum_term_bytes += (uint64_t)by_id[k]->len;

    double avg_token_len = (total_token_count ? (double)total_token_bytes / (double)total_token_count : 0.0);
    double avg_term_len  = (terms_count ? (double)sum_term_bytes / (double)terms_count : 0.0);

    FILE* out = std::fopen(out_bin, "wb");
    if (!out) die("cannot open out_bin");

    unsigned char zero[128]; std::memset(zero, 0, sizeof(zero));
    std::fwrite(zero, 1, sizeof(zero), out);

    uint64_t dict_offset = 128;

    for (uint32_t si = 0; si < terms_count; ++si) {
        TermEntry* e = by_id[term_ids[si]];
        wr_u32(out, e->len);
        std::fwrite(dict.pool.buf + e->off, 1, e->len, out);
        wr_u64(out, postings_off[si]);
        wr_u32(out, e->df);
        wr_u32(out, 0);
    }

    uint64_t dict_end = (uint64_t)std::ftell(out);
    uint64_t dict_bytes = dict_end - dict_offset;

    uint64_t postings_offset = dict_end;

    for (uint32_t si = 0; si < terms_count; ++si) {
        TermEntry* e = by_id[term_ids[si]];
        PostBlock* b = e->first;
        while (b) {
            for (uint32_t k = 0; k < b->used; ++k) wr_u32(out, b->doc[k]);
            b = b->next;
        }
    }

    uint64_t postings_end = (uint64_t)std::ftell(out);

    uint64_t docs_offset = postings_end;

    wr_u64(out, (uint64_t)docs_count);

    uint64_t* doc_off = (uint64_t*)std::malloc((size_t)docs_count * sizeof(uint64_t));
    if (!doc_off) die("doc_off OOM");

    uint64_t rel = 0;
    for (uint32_t id = 1; id <= docs_count; ++id) {
        const DocRec& r = docs[id - 1];
        doc_off[id - 1] = rel;
        rel += (uint64_t)(4 + 4 + 4 + 4 + r.title_len);
    }

    for (uint32_t k = 0; k < docs_count; ++k) wr_u64(out, doc_off[k]);

    for (uint32_t id = 1; id <= docs_count; ++id) {
        const DocRec& r = docs[id - 1];
        wr_u32(out, id);
        wr_u32(out, r.source_id);
        wr_u32(out, r.page_id);
        wr_u32(out, r.title_len);
        std::fwrite(title_pool.buf + r.title_off, 1, r.title_len, out);
    }

    uint64_t docs_end = (uint64_t)std::ftell(out);
    uint64_t docs_bytes = docs_end - docs_offset;

    std::fseek(out, 0, SEEK_SET);
    const char magic[8] = {'M','A','I','I','R','I','D','X'};
    std::fwrite(magic, 1, 8, out);
    wr_u32(out, 2);
    wr_u32(out, 0x3);
    wr_u64(out, (uint64_t)docs_count);
    wr_u64(out, (uint64_t)terms_count);
    wr_u64(out, dict_offset);
    wr_u64(out, dict_bytes);
    wr_u64(out, postings_offset);
    wr_u64(out, postings_bytes);
    wr_u64(out, docs_offset);
    wr_u64(out, docs_bytes);
    for (int z = 0; z < 6; ++z) wr_u64(out, 0);

    std::fclose(out);

    uint64_t t1 = now_qpc();
    double total_sec = qpc_seconds(t0, t1);
    double scan_sec  = qpc_seconds(t_scan0, t_scan1);

    double docs_per_sec = (double)docs_count / (scan_sec > 0 ? scan_sec : 1.0);
    double bytes_per_sec = (double)total_input_bytes / (scan_sec > 0 ? scan_sec : 1.0);
    double kb_per_sec = bytes_per_sec / 1024.0;

    std::fprintf(stderr,
        "DONE.\n"
        "docs=%u terms=%u\n"
        "avg_token_len_bytes=%.3f avg_term_len_bytes=%.3f\n"
        "scan_sec=%.3f total_sec=%.3f\n"
        "speed: docs/sec=%.2f KB/sec=%.2f\n"
        "index.bin: dict_bytes=%I64u postings_bytes=%I64u docs_bytes=%I64u\n",
        docs_count, terms_count,
        avg_token_len, avg_term_len,
        scan_sec, total_sec,
        docs_per_sec, kb_per_sec,
        (unsigned long long)dict_bytes,
        (unsigned long long)postings_bytes,
        (unsigned long long)docs_bytes
    );

    std::free(term_ids);
    std::free(postings_off);
    std::free(doc_off);
    std::free(by_id);
    std::free(dict.tab);
    std::free(dict.pool.buf);
    std::free(title_pool.buf);
    std::free(docs);

    return 0;
}
