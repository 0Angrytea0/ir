#include "freq.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

static bool read_line(FILE* f, std::string& out) {
    out.clear();
    int c;
    while ((c = std::fgetc(f)) != EOF) {
        if (c == '\r') continue;
        if (c == '\n') break;
        out.push_back((char)c);
    }
    if (c == EOF && out.empty()) return false;
    return true;
}

bool freq_add_file(FreqResult& fr, const char* tok_path) {
    FILE* f = std::fopen(tok_path, "rb");
    if (!f) return false;

    std::string line;
    while (read_line(f, line)) {
        if (line.empty()) continue;
        auto it = fr.term2cnt.find(line);
        if (it == fr.term2cnt.end()) fr.term2cnt.emplace(line, 1ULL);
        else it->second += 1ULL;
        fr.total_tokens += 1ULL;
    }

    std::fclose(f);
    return true;
}

std::vector<uint64_t> freq_sorted_counts_desc(const FreqResult& fr) {
    std::vector<uint64_t> v;
    v.reserve(fr.term2cnt.size());
    for (const auto& kv : fr.term2cnt) v.push_back(kv.second);
    std::sort(v.begin(), v.end(), [](uint64_t a, uint64_t b){ return a > b; });
    return v;
}

bool save_terms_tsv(const char* path, const FreqResult& fr) {
    FILE* out = std::fopen(path, "wb");
    if (!out) return false;
    std::fprintf(out, "term\tcount\n");
    for (const auto& kv : fr.term2cnt) {
        std::fwrite(kv.first.data(), 1, kv.first.size(), out);
        std::fprintf(out, "\t%llu\n", (unsigned long long)kv.second);
    }
    std::fclose(out);
    return true;
}

bool save_zipf_tsv(const char* path, const std::vector<uint64_t>& counts_desc) {
    FILE* out = std::fopen(path, "wb");
    if (!out) return false;
    std::fprintf(out, "rank\tfrequency\n");
    for (size_t i = 0; i < counts_desc.size(); ++i) {
        std::fprintf(out, "%llu\t%llu\n",
                     (unsigned long long)(i + 1ULL),
                     (unsigned long long)counts_desc[i]);
    }
    std::fclose(out);
    return true;
}
