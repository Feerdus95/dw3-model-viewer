/* ===== The DW2 and DCB tabs' disc prompt and bulk export =====
   The player's own disc (a .cue, .bin or .iso) is read in a thread by extract_dw2 / extract_dcb into the tab's folder
   (data/dw2, data/dcb, or the folder chosen before), the same folders the tabs list; and every model of a tab can be
   exported (glTF and OBJ) in one go. Index 0 is DW2, 1 DCB. */

struct DiscRead {
    std::thread th;
    ExtractProgress prog;
    std::atomic<bool> running{false}, finished{false};
    std::string err, note; /* the thread's, read after finished */
    ~DiscRead() {
        prog.cancel = true;
        if (th.joinable()) th.join();
    }
};
static DiscRead g_read[2];

static void disc_read_start(int game, const std::string &image) {
    DiscRead &r = g_read[game];
    if (r.running) return;
    if (r.th.joinable()) r.th.join();
    std::string &dir = game ? g_dcb : g_dw2;
    if (dir.empty()) {
        dir = (std::filesystem::path(g_data).parent_path() / (game ? "dcb" : "dw2")).u8string();
        game ? dcb_save_dir() : dw2_save_dir();
    }
    r.prog.cancel = false, r.prog.percent = 0, r.err.clear(), r.note.clear(), r.finished = false, r.running = true;
    r.th = std::thread([&r, game, image, dir]() {
        r.err = game ? extract_dcb(image, dir, r.prog) : extract_dw2(image, dir, r.prog);
        r.note = r.prog.get_note();
        r.finished = true, r.running = false;
    });
}

/* true once, on the frame after a run ended (its error, if any, stays in err) */
static bool disc_read_ended(int game) {
    DiscRead &r = g_read[game];
    if (r.running || !r.finished) return false;
    if (r.th.joinable()) r.th.join();
    r.finished = false;
    return true;
}

/* Every model of a tab as models/<dw2|dcb>/<name>/<name>.glb and .obj (the same files Model's buttons write) */
struct BulkExport {
    std::thread th;
    std::atomic<int> percent{0};
    std::atomic<bool> running{false}, finished{false}, cancel{false};
    std::string dir, note;
    ~BulkExport() {
        cancel = true;
        if (th.joinable()) th.join();
    }
};
static BulkExport g_bulk[2];

static void bulk_export_start(int game, std::vector<std::string> paths) {
    BulkExport &b = g_bulk[game];
    if (b.running) return;
    if (b.th.joinable()) b.th.join();
    b.dir = g_exe_dir + "models/" + (game ? "dcb" : "dw2");
    b.cancel = false, b.percent = 0, b.note.clear(), b.finished = false, b.running = true;
    b.th = std::thread([&b, paths]() {
        namespace fs = std::filesystem;
        std::error_code ec;
        int ok = 0;
        for (size_t i = 0; i < paths.size() && !b.cancel; i++) {
            std::vector<uint8_t> bytes;
            Model m;
            std::string name = fs::u8path(paths[i]).parent_path().filename().u8string(), out = b.dir + "/" + name, err;
            if (read_file(paths[i], bytes) && m.load(name, std::move(bytes))) {
                fs::create_directories(fs::u8path(out), ec);
                StageEdit e;
                StageInfo info;
                e.fromModel(m, info);
                ok += export_gltf(m, out, name, err) && e.exportObj(out, name);
            }
            b.percent = (int)(100 * (i + 1) / paths.size());
        }
        b.note = std::to_string(ok) + " of " + std::to_string(paths.size()) + " models exported to " + b.dir;
        b.finished = true, b.running = false;
    });
}
