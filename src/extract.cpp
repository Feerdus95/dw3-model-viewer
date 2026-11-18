#include "extract.h"

#include <windows.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

bool extract_write(const std::string &path, const Bytes &data) {
    std::ofstream f(fs::u8path(path), std::ios::binary);
    f.write((const char *)data.data(), (std::streamsize)data.size());
    return (bool)f;
}

/* OUT/.extract-tmp/<sub> -> OUT/<sub>, replacing what was there; then the temp folder goes */
bool extract_publish(const std::string &out, const char *const *subdirs, int n, std::string &err) {
    std::error_code ec;
    fs::path root = fs::u8path(out), tmp = root / ".extract-tmp";
    for (int i = 0; i < n; i++) {
        if (!fs::exists(tmp / subdirs[i], ec)) continue;
        fs::remove_all(root / subdirs[i], ec);
        fs::rename(tmp / subdirs[i], root / subdirs[i], ec);
        if (ec) return err = "Can't write to " + out + ": " + ec.message(), false;
    }
    fs::remove_all(tmp, ec);
    return true;
}
