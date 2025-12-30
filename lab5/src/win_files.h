#pragma once
#include <cstddef>

typedef void (*file_callback_t)(const char* full_path, const char* rel_path, void* user);

bool ensure_dir_exists(const char* path);
bool list_tok_files_rec(const char* root_dir, file_callback_t cb, void* user);
