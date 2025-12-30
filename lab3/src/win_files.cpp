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

static bool ends_with_txt_ci(const char* s) {
    if (!s) return false;
    size_t n = std::strlen(s);
    if (n < 4) return false;
    char c1 = s[n - 4], c2 = s[n - 3], c3 = s[n - 2], c4 = s[n - 1];
    if (c1 != '.') return false;
    if ((c2 == 't' || c2 == 'T') && (c3 == 'x' || c3 == 'X') && (c4 == 't' || c4 == 'T')) return true;
    return false;
}

static bool is_dot_dir(const char* name) {
    if (!name) return false;
    return (name[0] == '.' && name[1] == 0) || (name[0] == '.' && name[1] == '.' && name[2] == 0);
}

static bool list_txt_files_rec(const char* root, const char* dir_path, const char* rel_prefix,
                               file_callback_t cb, void* user) {
    char pattern[MAX_PATH];
    std::snprintf(pattern, sizeof(pattern), "%s\\*", dir_path);

    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(pattern, &ffd);
    if (h == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "FindFirstFileA failed for: %s\n", pattern);
        return false;
    }

    bool ok = true;
    do {
        const char* name = ffd.cFileName;
        if (is_dot_dir(name)) continue;

        char full[MAX_PATH];
        join_path(full, sizeof(full), dir_path, name);

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char next_rel[MAX_PATH];
            if (rel_prefix && rel_prefix[0]) {
                std::snprintf(next_rel, sizeof(next_rel), "%s\\%s", rel_prefix, name);
            } else {
                std::snprintf(next_rel, sizeof(next_rel), "%s", name);
            }
            if (!list_txt_files_rec(root, full, next_rel, cb, user)) {
                ok = false;
                break;
            }
            continue;
        }

        if (!ends_with_txt_ci(name)) continue;

        char rel[MAX_PATH];
        if (rel_prefix && rel_prefix[0]) {
            std::snprintf(rel, sizeof(rel), "%s\\%s", rel_prefix, name);
        } else {
            std::snprintf(rel, sizeof(rel), "%s", name);
        }
        cb(full, rel, user);

    } while (FindNextFileA(h, &ffd));

    FindClose(h);
    return ok;
}

bool list_txt_files(const char* dir_path, file_callback_t cb, void* user) {
    if (!dir_path || !cb) return false;
    return list_txt_files_rec(dir_path, dir_path, "", cb, user);
}
