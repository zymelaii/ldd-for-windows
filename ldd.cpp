#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>

struct DependencyWalkRecord {
    std::string deps_name;
    std::string resolved_path;
    UINT        err_code;
};

std::string format_winerr(int type, int err) {
    char buffer[1024]{};
    FormatMessage(type, 0, GetLastError(), 0, buffer, sizeof(buffer), 0);
    if (auto p = strchr(buffer, '\n')) { *p = '\0'; }
    return {buffer};
}

int walk_deps(
    const char                                       *path,
    std::function<void(const DependencyWalkRecord &)> callback) {
    auto module = LoadLibraryEx(path, NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!module) { return GetLastError(); }

    const auto base_addr  = reinterpret_cast<char *>(module);
    const auto header     = reinterpret_cast<PIMAGE_DOS_HEADER>(base_addr);
    const auto opt_header = reinterpret_cast<PIMAGE_OPTIONAL_HEADER>(
        base_addr + header->e_lfanew + 24);

    auto desc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
        base_addr
        + opt_header->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
              .VirtualAddress);

    char saved_cwd[MAX_PATH]{};
    GetCurrentDirectory(sizeof(saved_cwd), saved_cwd);

    char bin_cwd[MAX_PATH]{};
    GetModuleFileName(module, bin_cwd, MAX_PATH);
    PathRemoveFileSpec(bin_cwd);
    SetCurrentDirectory(bin_cwd);

    while (desc->FirstThunk) {
        const auto deps_name = reinterpret_cast<char *>(base_addr + desc->Name);

        DependencyWalkRecord record{};
        record.deps_name = deps_name;
        record.err_code  = NO_ERROR;

        auto deps_mod =
            LoadLibraryEx(deps_name, NULL, DONT_RESOLVE_DLL_REFERENCES);

        if (deps_mod) {
            char pathbuf[MAX_PATH]{};
            GetModuleFileName(deps_mod, pathbuf, sizeof(pathbuf));
            record.resolved_path = pathbuf;
        } else {
            record.err_code = GetLastError();
        }

        FreeLibrary(deps_mod);

        std::invoke(callback, record);

        ++desc;
    }

    FreeLibrary(module);
    return 0;
}

int main(int argc, char *argv[]) {
    const char *mod_path           = nullptr;
    bool        ignore_system_libs = false;

    if (argc >= 2) { mod_path = argv[1]; }

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--ignore-system-libs") == 0
            || strcmp(argv[i], "-u") == 0) {
            ignore_system_libs = true;
        }
    }

    if (!mod_path) {
        const auto help_fmt = R"(USAGE: %s <path-to-exe> [options]

OPTIONS:
  --ignore-system-libs, -u  ignore system libraries
)";
        fprintf(stderr, help_fmt, argv[0]);
        return EXIT_FAILURE;
    }

    std::vector<DependencyWalkRecord> records{};

    const int result =
        walk_deps(argv[1], [&records](const DependencyWalkRecord &record) {
            records.push_back(record);
        });

    if (result != NO_ERROR) {
        fprintf(
            stderr,
            "error: cannot open %s: %s\n",
            mod_path,
            format_winerr(FORMAT_MESSAGE_FROM_SYSTEM, result).c_str());
        return EXIT_FAILURE;
    }

    char sys_dir[MAX_PATH]{};
    GetSystemDirectory(sys_dir, sizeof(sys_dir));

    std::sort(
        records.begin(), records.end(), [](const auto &lhs, const auto &rhs) {
            return lhs.resolved_path < rhs.resolved_path;
        });

    for (const auto &record : records) {
        if (ignore_system_libs
            && PathIsPrefix(sys_dir, record.resolved_path.c_str())) {
            continue;
        }
        if (record.err_code == NO_ERROR) {
            printf(
                "%s => %s\n",
                record.deps_name.c_str(),
                record.resolved_path.c_str());
        } else {
            printf(
                "%s => UNRESOLVED (%u)\n",
                record.deps_name.c_str(),
                record.err_code);
        }
    }

    return EXIT_SUCCESS;
}
