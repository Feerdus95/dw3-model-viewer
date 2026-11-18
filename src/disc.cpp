#include "disc.h"

#include <windows.h>

#include <vector>

static FILE *open_utf8(const std::string &path) {
    wchar_t wide[1200];
    if (MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide, 1200) == 0) return nullptr;
    return _wfopen(wide, L"rb");
}

static std::string cue_file(const std::string &cue) {
    FILE *f = open_utf8(cue);
    if (!f) return "";
    std::string t;
    char buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) t.append(buf, n);
    fclose(f);
    for (size_t i = t.find("FILE"); i != std::string::npos; i = t.find("FILE", i + 1)) {
        size_t j = i + 4;
        while (j < t.size() && isspace((unsigned char)t[j])) j++;
        if (j == i + 4 || j >= t.size() || t[j] != '"') continue;
        size_t e = t.find('"', j + 1);
        if (e != std::string::npos && e > j + 1) return t.substr(j + 1, e - j - 1);
    }
    return "";
}

Disc::~Disc() {
    if (f_) fclose(f_);
}

bool Disc::open(const std::string &path, std::string &err) {
    std::string p = path;
    size_t dot = p.rfind('.');
    std::string ext = dot == std::string::npos ? "" : p.substr(dot);
    for (char &c : ext) c = (char)tolower((unsigned char)c);
    if (ext == ".cue") {
        FILE *probe = open_utf8(p);
        if (!probe) return err = "Can't open " + p, false;
        fclose(probe);
        std::string name = cue_file(p);
        if (name.empty()) return err = "The .cue doesn't name a .bin", false;
        size_t slash = p.find_last_of("/\\");
        p = (slash == std::string::npos ? "" : p.substr(0, slash + 1)) + name;
    }
    f_ = open_utf8(p);
    if (!f_) return err = "Can't open " + p, false;
    _fseeki64(f_, 0, SEEK_END);
    long long size = _ftelli64(f_);
    if (size < 17 * 2048) return err = "The disc image is too small", false;
    stride_ = size % 2352 == 0 ? 2352 : 2048;
    skip_ = stride_ == 2352 ? 24 : 0;
    return true;
}

Bytes Disc::sector(long n) {
    Bytes b(2048);
    if (_fseeki64(f_, (long long)n * stride_ + skip_, SEEK_SET) != 0) return Bytes();
    b.resize(fread(b.data(), 1, 2048, f_));
    return b;
}

Bytes Disc::read(long lba, long size) {
    Bytes out;
    if (size < 0) return out;
    for (long i = 0; i < (size + 2047) / 2048; i++) {
        Bytes s = sector(lba + i);
        out.insert(out.end(), s.begin(), s.end());
    }
    if ((long)out.size() > size) out.resize(size);
    return out;
}

const std::map<uint32_t, std::pair<std::string, uint32_t>> &Disc::files() {
    if (listed_) return files_;
    listed_ = true;
    struct Dir { uint32_t extent; int parent; std::string name; };
    Bytes pvdb = sector(16);
    if (pvdb.size() < 2048) return files_;
    Span pvd(pvdb);
    uint32_t lba = pvd.u32(140), size = pvd.u32(132);
    Bytes ptb = sector(lba); /* the path table's first sector, as the tool read it */
    Span pt(ptb);
    pt = pt.sub(0, size);
    std::vector<Dir> dirs;
    for (size_t p = 0; p < size && p < pt.n;) {
        int nl = pt.u8(p);
        std::string name;
        for (int i = 0; i < nl; i++) name += (char)pt.u8(p + 8 + i);
        dirs.push_back({pt.u32(p + 2), pt.u16(p + 6), name});
        p += 8 + nl + (nl & 1);
    }
    std::vector<std::string> full(dirs.size());
    for (size_t i = 0; i < dirs.size(); i++) /* a parent comes before its children in the table */
        full[i] = i == 0 ? "" : (dirs[i].parent >= 1 && (size_t)dirs[i].parent <= i ? full[dirs[i].parent - 1] : "") + dirs[i].name + "/";
    for (size_t i = 0; i < dirs.size(); i++) {
        Bytes first = sector(dirs[i].extent);
        if (first.size() < 14) continue;
        uint32_t dsize = Span(first).u32(10);
        Bytes db = read(dirs[i].extent, dsize);
        Span d(db);
        for (size_t q = 0; q < dsize && q + 33 <= d.n;) {
            int n = d.u8(q);
            if (n == 0) {
                q = (q / 2048 + 1) * 2048;
                continue;
            }
            int flags = d.u8(q + 25), nl = d.u8(q + 32);
            if (!(flags & 2)) {
                std::string name;
                for (int k = 0; k < nl && q + 33 + k < d.n; k++) name += (char)d.u8(q + 33 + k);
                name = name.substr(0, name.find(';'));
                files_[d.u32(q + 2)] = {full[i] + name, d.u32(q + 10)};
            }
            q += n;
        }
    }
    return files_;
}

bool Disc::find(const std::string &path, uint32_t &lba, uint32_t &size) {
    for (const auto &kv : files())
        if (kv.second.first == path) return lba = kv.first, size = kv.second.second, true;
    return false;
}
