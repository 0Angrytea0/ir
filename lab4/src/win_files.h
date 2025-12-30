#pragma once
#include <cstddef>

typedef void (*file_callback_t)(const char* full_path, const char* rel_path, void* user);

bool list_txt_files(const char* root_dir, file_callback_t cb, void* user);
bool ensure_dir_exists(const char* path);
