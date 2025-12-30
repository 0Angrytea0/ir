#include "win_files.h"
#include "tokenize.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <chrono>

struct Ctx {
    const char* out_dir;
    FILE* meta;
    unsigned long long total_docs;
    unsigned long long total_tokens;
    unsigned long long total_token_chars;
    unsigned long long total_bytes;
    std::chrono::steady_clock::time_point t0;
};

static uint64_t fnv1a64(const char* s) {
    const uint64_t FNV_OFFSET = 14695981039346656037ULL;
    const uint64_t FNV_PRIME  = 1099511628211ULL;
    uint64_t h = FNV_OFFSET;
    if (!s) return h;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        h ^= (uint64_t)(*p);
        h *= FNV_PRIME;
    }
    return h;
}

static void hex16(char out[17], uint64_t v) {
    static const char* H = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        int shift = (15 - i) * 4;
        out[i] = H[(v >> shift) & 0xFULL];
    }
    out[16] = 0;
}

static void join_path(char* out, size_t out_sz, const char* a, const char* b) {
    std::snprintf(out, out_sz, "%s\\%s", a, b);
}

static void on_file(const char* full_path, const char* rel_path, void* user) {
    Ctx* ctx = (Ctx*)user;

    uint64_t h = fnv1a64(rel_path);
    char hex[17];
    hex16(hex, h);

    char out_name[260];
    std::snprintf(out_name, sizeof(out_name), "%s.tok", hex);

    char out_path[520];
    join_path(out_path, sizeof(out_path), ctx->out_dir, out_name);

    FILE* fout = std::fopen(out_path, "wb");
    if (!fout) {
        std::fprintf(stderr, "[err] cannot open output: %s\n", out_path);
        return;
    }

    TokenizeStats st;
    bool ok = tokenize_file_to_stream(full_path, fout, &st);
    std::fclose(fout);

    if (!ok) {
        std::fprintf(stderr, "[err] tokenize failed: %s\n", full_path);
        return;
    }

    std::fprintf(ctx->meta, "%s\t%s\t%I64u\t%I64u\t%I64u\n",
                 rel_path,
                 out_name,
                 (unsigned long long)st.tokens_out,
                 (unsigned long long)st.token_chars_sum,
                 (unsigned long long)st.bytes_in);

    ctx->total_docs++;
    ctx->total_tokens += st.tokens_out;
    ctx->total_token_chars += st.token_chars_sum;
    ctx->total_bytes  += st.bytes_in;

    if (ctx->total_docs % 1000ULL == 0ULL) {
        auto now = std::chrono::steady_clock::now();
        double sec = std::chrono::duration<double>(now - ctx->t0).count();
        double kb = (double)ctx->total_bytes / 1024.0;
        double kbps = (sec > 0.0 ? (kb / sec) : 0.0);
        std::fprintf(stderr, "[prog] docs=%I64u tokens=%I64u bytes=%I64u time=%.3fs speed=%.2f KB/s\n",
                     ctx->total_docs, ctx->total_tokens, ctx->total_bytes, sec, kbps);
    }
}

static void usage() {
    std::fprintf(stderr,
        "Usage:\n"
        "  tokenize.exe <input_dir> <out_tokens_dir> <meta_out_tsv>\n"
        "Example:\n"
        "  tokenize.exe corpus out\\tokens out\\tokens_meta.tsv\n");
}

int main(int argc, char** argv) {
    if (argc != 4) {
        usage();
        return 2;
    }

    const char* input_dir = argv[1];
    const char* out_dir   = argv[2];
    const char* meta_out  = argv[3];

    if (!ensure_dir_exists("out")) {
        std::fprintf(stderr, "Cannot ensure 'out' directory\n");
    }
    if (!ensure_dir_exists(out_dir)) {
        std::fprintf(stderr, "Cannot create out dir: %s\n", out_dir);
        return 1;
    }

    FILE* meta = std::fopen(meta_out, "wb");
    if (!meta) {
        std::fprintf(stderr, "Cannot open meta file: %s\n", meta_out);
        return 1;
    }

    std::fprintf(meta, "doc_path\ttok_file\ttokens_count\ttoken_chars\tbytes_in\n");

    Ctx ctx;
    ctx.out_dir = out_dir;
    ctx.meta = meta;
    ctx.total_docs = 0;
    ctx.total_tokens = 0;
    ctx.total_token_chars = 0;
    ctx.total_bytes = 0;
    ctx.t0 = std::chrono::steady_clock::now();

    bool ok = list_txt_files(input_dir, on_file, &ctx);
    std::fclose(meta);

    if (!ok) {
        std::fprintf(stderr, "File enumeration failed.\n");
        return 1;
    }

    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - ctx.t0).count();
    double kb = (double)ctx.total_bytes / 1024.0;
    double kbps = (sec > 0.0 ? (kb / sec) : 0.0);
    double avg_tok_len = (ctx.total_tokens > 0 ? (double)ctx.total_token_chars / (double)ctx.total_tokens : 0.0);
    double tok_per_kb = (kb > 0.0 ? (double)ctx.total_tokens / kb : 0.0);

    std::fprintf(stderr,
                 "Done. docs=%I64u tokens=%I64u bytes=%I64u token_chars=%I64u\n",
                 ctx.total_docs, ctx.total_tokens, ctx.total_bytes, ctx.total_token_chars);
    std::fprintf(stderr, "Time: %.6f s\n", sec);
    std::fprintf(stderr, "Avg token length: %.4f chars\n", avg_tok_len);
    std::fprintf(stderr, "Speed: %.2f KB/s\n", kbps);
    std::fprintf(stderr, "Tokens per KB: %.2f\n", tok_per_kb);

    return 0;
}
