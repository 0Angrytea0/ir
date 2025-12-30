#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct FreqResult {
    std::unordered_map<std::string, uint64_t> term2cnt;
    uint64_t total_tokens;
};

bool freq_add_file(FreqResult& fr, const char* tok_path);
std::vector<uint64_t> freq_sorted_counts_desc(const FreqResult& fr);
bool save_terms_tsv(const char* path, const FreqResult& fr);
bool save_zipf_tsv(const char* path, const std::vector<uint64_t>& counts_desc);
