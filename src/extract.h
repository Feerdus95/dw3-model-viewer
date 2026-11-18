/* Reads the Digimon models of the player's own Digimon World 2 (USA, SLUS-01193) and Digital Card Battle discs into the
   folders the viewer's DW2 and DCB tabs list: OUT/digimon/NNN_Name/NNN_Name.dw2m and OUT/fx/CODE/CODE.dw2m, or
   OUT/digimon/ID_Name/ID_Name.dcbm. Formats: tools/extract_dw2.py and tools/extract_dcb.py (kept as the reference), which
   these replace. Everything goes to OUT/.extract-tmp first and moves into place only when complete, so a cancel leaves
   OUT as it was. */
#pragma once
#include <atomic>
#include <mutex>
#include <string>

#include "bytes.h"

struct ExtractProgress {
    std::atomic<int> percent{0};
    std::atomic<bool> cancel{false};
    std::mutex m; /* guards note */
    std::string note;
    void set_note(const std::string &s) {
        std::lock_guard<std::mutex> l(m);
        note = s;
    }
    std::string get_note() { std::lock_guard<std::mutex> l(m); return note; }
};

/* "" when done, else why not (in English, for the window) */
std::string extract_dw2(const std::string &image, const std::string &out, ExtractProgress &p);
std::string extract_dcb(const std::string &image, const std::string &out, ExtractProgress &p);

/* shared by the two */
bool extract_publish(const std::string &out, const char *const *subdirs, int n, std::string &err);
bool extract_write(const std::string &path, const Bytes &data);
