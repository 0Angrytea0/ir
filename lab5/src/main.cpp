#include "win_files.h"
#include "freq.h"
#include <cstdio>
#include <cstring>

struct Ctx {
    FreqResult* fr;
    unsigned long long files_ok;
    unsigned long long files_fail;
};

static void on_tok(const char* full_path, const char* rel_path, void* user) {
    (void)rel_path;
    Ctx* ctx = (Ctx*)user;
    if (freq_add_file(*ctx->fr, full_path)) ctx->files_ok++;
    else ctx->files_fail++;
    if ((ctx->files_ok + ctx->files_fail) % 2000ULL == 0ULL) {
        std::fprintf(stderr, "[prog] files=%llu ok=%llu fail=%llu terms=%zu tokens=%llu\n",
                     (unsigned long long)(ctx->files_ok + ctx->files_fail),
                     (unsigned long long)ctx->files_ok,
                     (unsigned long long)ctx->files_fail,
                     ctx->fr->term2cnt.size(),
                     (unsigned long long)ctx->fr->total_tokens);
    }
}

static void usage() {
    std::fprintf(stderr,
        "Usage:\n"
        "  zipf.exe <tokens_root_dir> <out_zipf_tsv> <out_terms_tsv>\n"
        "Example:\n"
        "  zipf.exe out\\tokens out\\zipf_raw.tsv out\\terms_raw.tsv\n"
        "  zipf.exe out\\stem_tokens out\\zipf_stem.tsv out\\terms_stem.tsv\n");
}

int main(int argc, char** argv) {
    if (argc != 4) {
        usage();
        return 2;
    }

    const char* tokens_root = argv[1];
    const char* out_zipf = argv[2];
    const char* out_terms = argv[3];

    FreqResult fr;
    fr.total_tokens = 0;

    Ctx ctx;
    ctx.fr = &fr;
    ctx.files_ok = 0;
    ctx.files_fail = 0;

    bool ok = list_tok_files_rec(tokens_root, on_tok, &ctx);
    if (!ok) {
        std::fprintf(stderr, "Failed to enumerate token files in: %s\n", tokens_root);
        return 1;
    }

    auto counts = freq_sorted_counts_desc(fr);

    if (!save_terms_tsv(out_terms, fr)) {
        std::fprintf(stderr, "Cannot write terms file: %s\n", out_terms);
        return 1;
    }
    if (!save_zipf_tsv(out_zipf, counts)) {
        std::fprintf(stderr, "Cannot write zipf file: %s\n", out_zipf);
        return 1;
    }

    std::fprintf(stderr, "Done. files_ok=%llu files_fail=%llu unique_terms=%zu total_tokens=%llu\n",
                 (unsigned long long)ctx.files_ok,
                 (unsigned long long)ctx.files_fail,
                 fr.term2cnt.size(),
                 (unsigned long long)fr.total_tokens);

    return 0;
}
