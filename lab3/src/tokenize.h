#pragma once
#include <cstddef>
#include <cstdio>

struct TokenizeStats {
    unsigned long long bytes_in;
    unsigned long long tokens_out;
    unsigned long long token_chars_sum;
};

bool tokenize_file_to_stream(const char* input_path, FILE* out, TokenizeStats* st);
