/* Digital Card Battle: the Digimon models of F.DRV and G.DRV as .dcbm files (tools/extract_dcb.py). The game's files sit in
   A..P.DRV archives (32-byte records: key, sector, size, name; key 0x80 is a folder). */
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <thread>

#include "dcb.h"
#include "disc.h"
#include "extract.h"

namespace fs = std::filesystem;
namespace {

std::string cp932(const uint8_t *p, size_t n) {
    std::string s((const char *)p, n);
    s = s.substr(0, s.find('\0'));
    wchar_t w[64];
    int len = MultiByteToWideChar(932, 0, s.c_str(), -1, w, 64);
    char u[256];
    if (len > 0 && WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, nullptr, nullptr) > 0) s = u;
    std::replace(s.begin(), s.end(), '/', '_');
    return s;
}

typedef std::vector<std::pair<std::string, Bytes>> Files;

void drv_files(Span data, int32_t sector, const std::string &path, std::vector<int32_t> &seen, Files &out) {
    if (std::find(seen.begin(), seen.end(), sector) != seen.end()) return;
    seen.push_back(sector);
    for (size_t o = (size_t)sector * 2048; o + 32 <= data.n; o += 32) {
        int32_t key = data.s32(o), sec = data.s32(o + 4), size = data.s32(o + 8);
        if (key == 0) break;
        std::string name = cp932(data.p + o + 16, 16);
        if (key == 0x80) drv_files(data, sec, path + name + "/", seen, out);
        else {
            Span f = data.sub((size_t)sec * 2048, size);
            std::string full = path + name;
            auto it = std::find_if(out.begin(), out.end(), [&](const auto &p) { return p.first == full; });
            Bytes b(f.p, f.p + f.n);
            if (it == out.end()) out.push_back({full, std::move(b)});
            else it->second = std::move(b);
        }
    }
}

/* CARD2: a header, then the Digimon cards (0x13C bytes: name at +3, model id at +0xE5) */
std::map<int, std::string> card_names(Span c) {
    std::map<int, std::string> names;
    int n = c.u16(4);
    for (int i = 0; i < n; i++) {
        size_t o = 8 + (size_t)i * 0x13C;
        std::string s;
        for (size_t k = o + 3; k < o + 0x19 && c.u8(k); k++) { /* latin-1 to UTF-8 */
            int ch = c.u8(k);
            if (ch < 0x80) s += (char)ch;
            else s += (char)(0xC0 | ch >> 6), s += (char)(0x80 | (ch & 0x3F));
        }
        names[c.u8(o + 0xE5)] = s;
    }
    return names;
}

} // namespace

std::string extract_dcb(const std::string &image, const std::string &out, ExtractProgress &prog) {
    Disc disc;
    std::string err;
    if (!disc.open(image, err)) return err;
    auto drv = [&](const char *n, Files &f) {
        uint32_t lba, size;
        if (!disc.find(std::string(n) + ".DRV", lba, size)) return false;
        Bytes d = disc.read(lba, size);
        std::vector<int32_t> seen;
        try {
            drv_files(d, 0, "", seen, f);
        } catch (const std::exception &) {
        }
        return true;
    };
    Files bf, ff, gf;
    for (auto &p : std::initializer_list<std::pair<const char *, Files *>>{{"B", &bf}, {"F", &ff}, {"G", &gf}})
        if (!drv(p.first, *p.second)) return std::string("This is not a Digimon Digital Card Battle disc (no ") + p.first + ".DRV).";
    std::map<int, std::string> names;
    try {
        auto it = std::find_if(bf.begin(), bf.end(), [](const auto &p) { return p.first == "CARD2"; });
        if (it == bf.end()) return "This is not a Digimon Digital Card Battle disc (no CARD2).";
        names = card_names(it->second);
    } catch (const std::exception &) {
        return "This is not a Digimon Digital Card Battle disc (CARD2 is unreadable).";
    }
    std::error_code ec;
    fs::path tmp = fs::u8path(out) / ".extract-tmp";
    fs::remove_all(tmp, ec);
    struct Job { std::string fname; const Bytes *b; };
    std::vector<Job> jobs;
    for (auto &p : ff) jobs.push_back({"F/" + p.first, &p.second});
    for (auto &p : gf) jobs.push_back({"G/" + p.first, &p.second});
    std::atomic<size_t> next{0}, done{0}, ok{0};
    std::mutex logm;
    std::string log;
    auto worker = [&]() {
        for (size_t i; !prog.cancel && (i = next++) < jobs.size();) {
            const Job &j = jobs[i];
            try {
                Span b(*j.b);
                int mid = 0;
                bool found = false;
                for (const DcbChunk &c : dcb_chunks(b))
                    if (c.id == 0) {
                        mid = c.sub, found = true;
                        break;
                    }
                if (!found) throw std::runtime_error("no model chunk");
                std::string nm = names.count(mid) ? names[mid] : "model";
                std::replace(nm.begin(), nm.end(), ' ', '_');
                nm.erase(std::remove(nm.begin(), nm.end(), '*'), nm.end());
                std::string name = std::to_string(mid) + "_" + nm + (j.fname.compare(0, 2, "G/") == 0 ? "_G" + j.fname.substr(2) : "");
                dcb_convert(b, tmp.u8string(), name);
                ok++;
            } catch (const std::exception &e) {
                std::lock_guard<std::mutex> l(logm);
                log += (log.empty() ? "" : "\n") + j.fname + " skipped " + e.what();
            }
            prog.percent = (int)(99 * ++done / jobs.size());
        }
    };
    std::vector<std::thread> pool;
    unsigned n = std::max(1u, std::min(4u, std::thread::hardware_concurrency()));
    for (unsigned i = 0; i < n; i++) pool.emplace_back(worker);
    for (auto &t : pool) t.join();
    if (prog.cancel) return fs::remove_all(tmp, ec), "Cancelled.";
    if (ok == 0) return fs::remove_all(tmp, ec), "No model could be read from this disc." + (log.empty() ? "" : "\n" + log);
    static const char *const SUB[] = {"digimon"};
    if (!extract_publish(out, SUB, 1, err)) return err;
    prog.percent = 100;
    prog.set_note(std::to_string(ok.load()) + " models written (the rest of F/G are not Digimon models)");
    return "";
}
