#include "win_files.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

bool ensure_dir_exists(const char* path) {
    if (!path || !path[0]) return false;
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;
    if (CreateDirectoryA(path, nullptr)) return true;
    attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

static void join_path(char* out, size_t out_sz, const char* a, const char* b) {
    if (!out || out_sz == 0) return;
    out[0] = 0;
    std::snprintf(out, out_sz, "%s\\%s", a, b);
}

static bool list_files_by_pattern(const char* dir_path, const char* pattern_ext, file_callback_t cb, void* user) {
    if (!dir_path || !cb || !pattern_ext) return false;

    char pattern[MAX_PATH];
    std::snprintf(pattern, sizeof(pattern), "%s\\%s", dir_path, pattern_ext);

    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(pattern, &ffd);
    if (h == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "FindFirstFileA failed for: %s\n", pattern);
        return false;
    }

    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char full[MAX_PATH];
        join_path(full, sizeof(full), dir_path, ffd.cFileName);
        cb(full, ffd.cFileName, user);
    } while (FindNextFileA(h, &ffd));

    FindClose(h);
    return true;
}

bool list_txt_files(const char* dir_path, file_callback_t cb, void* user) {
    return list_files_by_pattern(dir_path, "*.txt", cb, user);
}

bool list_tok_files(const char* dir_path, file_callback_t cb, void* user) {
    return list_files_by_pattern(dir_path, "*.tok", cb, user);
}
