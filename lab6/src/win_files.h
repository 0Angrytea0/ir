#pragma once
#include <cstddef>

typedef void (*file_callback_t)(const char* full_path, const char* file_name, void* user);

bool list_txt_files(const char* dir_path, file_callback_t cb, void* user);
bool list_tok_files(const char* dir_path, file_callback_t cb, void* user);
bool ensure_dir_exists(const char* path);
