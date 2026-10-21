/* --- the files --- */

static std::string g_data;                  /* data/files */
static std::vector<std::string> g_names;    /* the disc's names, by fid */
static std::string g_exe_dir;
static GameData g_game;                     /* fighters, stages, names */

/* the window and the panels, kept between runs in settings.ini beside the exe (sizes in points, not pixels) */
struct Settings {
    int w = 1440, h = 900, x = 0, y = 0;
    bool max = false, hasPos = false, browser = true, inspector = true, hints = true;
    float bw = 0, iw = 0;
};
static Settings g_cfg;

static void cfg_load() {
    std::ifstream f(g_exe_dir + "settings.ini");
    std::string k;
    double v;
    while (f >> k >> v) {
        if (k == "w") g_cfg.w = std::clamp((int)v, 640, 16384);
        else if (k == "h") g_cfg.h = std::clamp((int)v, 480, 16384);
        else if (k == "x") g_cfg.x = (int)v, g_cfg.hasPos = true;
        else if (k == "y") g_cfg.y = (int)v;
        else if (k == "max") g_cfg.max = v != 0;
        else if (k == "browser") g_cfg.browser = v != 0;
        else if (k == "inspector") g_cfg.inspector = v != 0;
        else if (k == "hints") g_cfg.hints = v != 0;
        else if (k == "browserw") g_cfg.bw = (float)v;
        else if (k == "inspectorw") g_cfg.iw = (float)v;
    }
}

static void cfg_save() {
    std::ofstream f(g_exe_dir + "settings.ini");
    f << "w " << g_cfg.w << "\nh " << g_cfg.h << "\nx " << g_cfg.x << "\ny " << g_cfg.y << "\nmax " << g_cfg.max
      << "\nbrowser " << g_cfg.browser << "\ninspector " << g_cfg.inspector << "\nhints " << g_cfg.hints
      << "\nbrowserw " << g_cfg.bw << "\ninspectorw " << g_cfg.iw << "\n";
}

static bool read_file(const std::string &path, std::vector<uint8_t> &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

static std::string fid_path(int fid) {
    char b[16];
    snprintf(b, sizeof b, "/%04d.bin", fid);
    return g_data + b;
}

static std::string fid_name(int fid) {
    return fid < (int)g_names.size() ? g_names[fid] : "#" + std::to_string(fid);
}

/* The data tools/setup_disc.py writes: files/<fid>.bin and names.txt, in
   the folder given, the one picked last (data_dir.txt by the exe), data/
   (from the exe in build/) or next to the exe. Whether one was found. */
static bool find_data(const std::string &given) {
    std::vector<std::string> tries = {g_exe_dir + "../data", g_exe_dir + "data"};
    std::ifstream saved(g_exe_dir + "data_dir.txt");
    std::string line;
    if (saved && std::getline(saved, line) && !line.empty()) tries.insert(tries.begin(), line);
    if (!given.empty()) tries.insert(tries.begin(), given);
    for (const std::string &dir : tries) {
        std::ifstream names(dir + "/names.txt");
        if (!names) continue;
        g_data = dir + "/files";
        g_names.clear();
        g_game = GameData();
        for (std::string line; std::getline(names, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            g_names.push_back(line);
        }
        g_game.load(g_data);
        return true;
    }
    g_data = tries[0] + "/files";
    return false;
}

/* The disc's models: the files there whose names start M (FIGHT/MODEL,
   FIGHT/EFFECT) and that have a bone list */
struct Entry {
    int fid;
    std::string name;
    std::string label; /* who it is: the fighters' names, or the technique's */
};

static std::vector<Entry> scan_models() {
    std::vector<Entry> out;
    std::vector<uint8_t> bytes;
    for (int fid = 1; fid < (int)g_names.size(); fid++) {
        const std::string &n = g_names[fid];
        if (n.empty() || n[0] != 'M' || !read_file(fid_path(fid), bytes)) continue;
        Archive a{bytes.data(), bytes.size()};
        if (a.count() > 0 && (size_t)a.count() * 4 <= a.size && find_bone_entry(a) >= 0) out.push_back({fid, n, g_game.fileLabel(fid, n)});
    }
    return out;
}


/* --- Digimon World 2: the folder dw2-extractor made (digimon/ and fx/ in it, a .dw2m in each of their folders), kept in dw2_dir.txt --- */

struct Dw2Entry {
    std::string label, path;
    bool fx = false;
};
static std::string g_dw2;

static void dw2_load_dir() {
    std::ifstream f(g_exe_dir + "dw2_dir.txt");
    std::getline(f, g_dw2);
    while (!g_dw2.empty() && (g_dw2.back() == '\r' || g_dw2.back() == '\n')) g_dw2.pop_back();
}
static void dw2_save_dir() { std::ofstream(g_exe_dir + "dw2_dir.txt") << g_dw2 << "\n"; }

static std::vector<Dw2Entry> dw2_scan() {
    namespace fs = std::filesystem;
    std::vector<Dw2Entry> out;
    std::error_code ec;
    for (int fx = 0; fx < 2; fx++) {
        fs::path root = fs::u8path(g_dw2) / (fx ? "fx" : "digimon");
        for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            std::string dir = it->path().filename().u8string();
            fs::path file = it->path() / (dir + ".dw2m");
            if (!fs::exists(file, ec)) continue;
            Dw2Entry e;
            size_t us = dir.find('_');
            e.label = us == std::string::npos ? dir : dir.substr(0, us) + "  " + dir.substr(us + 1);
            e.path = file.u8string();
            e.fx = fx;
            out.push_back(e);
        }
    }
    std::sort(out.begin(), out.end(), [](const Dw2Entry &a, const Dw2Entry &b) { return a.fx != b.fx ? a.fx < b.fx : a.label < b.label; });
    return out;
}
