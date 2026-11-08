#include "script_mod.h"
#include <cstdio>
#include <cstring>

static std::string fmt(const char *f, ...) {
    char b[512];
    va_list a;
    va_start(a, f);
    vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}

static const char *const WHO[] = {"own", "partner1", "partner2", "partner3", "enemy1", "enemy2", "enemy3"};
static const int MAX_TICKS = 600, MAX_COMMANDS = 500, MAX_WORDS = 4096, SOUND_HIT_AWARE = 0x4000;

int script_cmd_len(const int *w, int n) {
    switch (w[0]) {
    case 0: case 8: case 9: case 0xFF: return 1;
    case 1: case 11: return 2;
    case 10: case 7: return 3;
    }
    if (n < 2) return 0;
    switch (w[0]) {
    case 2: {
        static const int LEN[] = {4, 3, 3, 7, 7, 4, 4, 4, 4, 3};
        if (w[1] < 0 || w[1] > 9) return 0;
        return w[1] == 5 && n > 3 && w[3] == 4 ? 5 : LEN[w[1]];
    }
    case 3: return w[1] == 0 ? 6 : 3;
    case 4: return w[1] == 0 ? 5 : 3;
    case 5: return n < 3 ? 0 : w[2] == 1 ? 17 : 4;
    case 6: return w[1] == 0 ? 9 : 3;
    }
    return 0;
}

ScriptWords script_split(const uint8_t *p, const uint8_t *end) {
    ScriptWords out;
    std::vector<int> all;
    for (; p + 2 <= end && all.size() < 4200; p += 2) {
        int16_t v;
        memcpy(&v, p, 2);
        all.push_back(v);
    }
    size_t at = 0;
    while (at < all.size() && (int)out.size() < MAX_COMMANDS) {
        int len = script_cmd_len(&all[at], (int)(all.size() - at));
        if (len == 0 || at + len > all.size()) break; /* the game would stop at what it doesn't know */
        ScriptCmd c;
        c.w.assign(all.begin() + at, all.begin() + at + len);
        out.push_back(c);
        at += len;
        if (c.w[0] == 0 || c.w[0] == 0xFF) break;
    }
    /* the game ends a script at 0 or 0xFF; the file's "end" is both, and a script the game stops in by itself gets one */
    if (out.empty() || (out.back().w[0] != 0 && out.back().w[0] != 0xFF)) out.push_back(ScriptCmd{{0}, ""});
    out.back().w[0] = 0;
    return out;
}

std::vector<ScriptLine> script_lines(const ScriptWords &s) {
    std::vector<ScriptLine> out;
    for (const ScriptCmd &c : s) {
        std::vector<ScriptLine> l = read_script_words(c.w);
        ScriptLine ln = l.empty() ? ScriptLine() : l[0];
        if (c.w[0] == 10) {
            int snd = c.w[1] & ~SOUND_HIT_AWARE;
            std::string what = !c.sound.empty() ? c.sound : snd == 0x62 || snd == 0x63 ? "the hit's sound" : "game sound " + std::to_string(snd);
            ln.text = "sound: " + what + (c.w[1] & SOUND_HIT_AWARE ? " (the game's own on a result 3 hit)" : "") + fmt(" for %d", c.w.size() > 2 ? c.w[2] : 0);
        }
        out.push_back(ln);
    }
    return out;
}

/* --- command to the file's JSON --- */

static std::string esc(const std::string &s) {
    std::string o;
    for (char c : s) o += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
    return o;
}

static std::string named(const ScriptCmd &c) {
    const std::vector<int> &w = c.w;
    auto v3 = [&](int i) { return fmt("[%d, %d, %d]", w[i], w[i + 1], w[i + 2]); };
    switch (w[0]) {
    case 0: return "{\"op\": \"end\"}";
    case 1:
        if (w[1] == 0 || w[1] == 5) return fmt("{\"op\": \"hit\", \"when\": \"%s\"}", w[1] == 0 ? "last" : "next");
        break;
    case 2: {
        if (w[2] < 0 || w[2] > 6) break;
        std::string head = fmt("{\"op\": \"model\", \"who\": \"%s\", \"do\": ", WHO[w[2]]);
        switch (w[1]) {
        case 0: return head + (w[3] == 0 ? "\"wait_motion\"}" : w[3] == 1 ? "\"idle\"}" : fmt("\"motion\", \"n\": %d}", w[3]));
        case 1: return head + "\"show\"}";
        case 2: return head + "\"hide\"}";
        case 3: return head + fmt("\"move\", \"to\": %s, \"time\": %d}", v3(3).c_str(), w[6]);
        case 4: if (w[6] == 0) return head + fmt("\"turn\", \"to\": %s}", v3(3).c_str()); break;
        case 5:
            if (w[3] == 0) return head + "\"wait_jump\"}";
            if (w[3] == 4) return head + fmt("\"jump\", \"n\": 4, \"distance\": %d}", w[4]);
            return head + fmt("\"jump\", \"n\": %d}", w[3]);
        case 6: return head + fmt("\"add_fighter\", \"fighter\": %d}", w[3]);
        case 7: return head + fmt("\"home\", \"time\": %d}", w[3]);
        case 8: return head + fmt("\"home_offset\", \"time\": %d}", w[3]);
        case 9: return head + "\"remove\"}";
        }
        break;
    }
    case 3: {
        std::string e = w[2] == 9999 ? "\"own\"" : std::to_string(w[2]);
        return w[1] == 1 ? fmt("{\"op\": \"effect\", \"mode\": \"load\", \"effect\": %s}", e.c_str())
                         : fmt("{\"op\": \"effect\", \"mode\": \"play\", \"effect\": %s, \"at\": %s}", e.c_str(), v3(3).c_str());
    }
    case 4: {
        std::string st = w[2] == 0x38 ? "\"own\"" : std::to_string(w[2]);
        return w[1] == 1 ? fmt("{\"op\": \"stage\", \"mode\": \"wait\", \"stage\": %s}", st.c_str())
                         : fmt("{\"op\": \"stage\", \"mode\": \"change\", \"stage\": %s, \"fade_out\": %d, \"fade_in\": %d}", st.c_str(), w[3], w[4]);
    }
    case 5:
        if (w[2] == 1)
            return fmt("{\"op\": \"camera\", \"time\": %d, \"at\": %s, \"look\": %s, \"aim\": %s, \"turn\": %s, \"rz\": %d, \"projection\": %d}", w[1], v3(3).c_str(),
                       v3(6).c_str(), v3(9).c_str(), v3(12).c_str(), w[15], w[16]);
        if (w[3] >= 0 && w[3] <= 7) return fmt("{\"op\": \"camera\", \"time\": %d, \"shot\": %d}", w[1], w[3] == 0 ? 8 : w[3]);
        break;
    case 6:
        return w[1] == 1 ? fmt("{\"op\": \"effect_model\", \"mode\": \"wait\", \"model\": %d}", w[2])
                         : fmt("{\"op\": \"effect_model\", \"mode\": \"play\", \"model\": %d, \"at\": %s, \"turn\": %s}", w[2], v3(3).c_str(), v3(6).c_str());
    case 7: return fmt("{\"op\": \"flash\", \"mode\": \"%s\", \"time\": %d}", w[1] == 0 ? "start" : "end", w[2]);
    case 10: {
        int snd = w[1] & ~SOUND_HIT_AWARE;
        std::string s = !c.sound.empty() ? "\"" + esc(c.sound) + "\"" : snd == 0x62 ? "\"hit\"" : snd == 0x63 ? "\"hit_last\"" : std::to_string(snd);
        return fmt("{\"op\": \"sound\", \"sound\": %s, \"time\": %d%s}", s.c_str(), w[2], w[1] & SOUND_HIT_AWARE ? ", \"hit\": true" : "");
    }
    case 11: return fmt("{\"op\": \"wait\", \"ticks\": %d}", w[1]);
    }
    return "";
}

/* --- the file's JSON to a command (the port's scriptmod.c rules) --- */

namespace {
struct Comp {
    std::vector<int> w;
    std::string sound, err;
    bool fail(const std::string &m) { return err = m, false; }
    bool emit(int v) { return w.push_back(v), true; }
    bool num(const Json &o, const char *key, int lo, int hi, int &v, bool opt = false, int def = 0) {
        const Json *j = o.get(key);
        if (!j) return opt ? (v = def, true) : fail(fmt("no \"%s\"", key));
        if (j->type != 'n' || j->num != (double)(int)j->num || j->num < lo || j->num > hi) return fail(fmt("\"%s\" is not a whole number from %d to %d", key, lo, hi));
        return v = (int)j->num, true;
    }
    bool word(const Json &o, const char *key, const char *const *names, int count, int &which) {
        const Json *j = o.get(key);
        if (!j || j->type != 's') return fail(fmt("no \"%s\"", key));
        for (int i = 0; i < count; i++)
            if (j->str == names[i]) return which = i, true;
        return fail(fmt("\"%s\": \"%s\" is not a known word", key, j->str.c_str()));
    }
    bool vec(const Json &o, const char *key, int n, bool opt, int *v) {
        const Json *j = o.get(key);
        if (!j && opt) return memset(v, 0, n * sizeof *v), true;
        if (!j || j->type != '[' || (int)j->items.size() != n) return fail(fmt("\"%s\" is a list of %d numbers", key, n));
        for (int i = 0; i < n; i++) {
            const Json &e = j->items[i].second;
            if (e.type != 'n' || e.num != (double)(int)e.num || e.num < -32768 || e.num > 32767) return fail(fmt("\"%s\" has a number out of range", key));
            v[i] = (int)e.num;
        }
        return true;
    }
    bool run(const Json &o);
};

bool Comp::run(const Json &o) {
    static const char *const OPS[] = {"hit", "model", "effect", "stage", "camera", "effect_model", "flash", "sound", "wait", "end", "raw"};
    static const char *const DO[] = {"wait_motion", "idle", "motion", "show", "hide", "move", "turn", "jump", "wait_jump", "add_fighter", "home", "home_offset", "remove"};
    static const char *const M2[] = {"play", "load"}, *const MEM[] = {"play", "wait"}, *const MS[] = {"change", "wait"}, *const MF[] = {"start", "end"};
    static const char *const WHEN[] = {"last", "next"};
    int op, who, what, a, t, v[14];
    if (o.type != '{') return fail("is not an object");
    if (!word(o, "op", OPS, 11, op)) return false;
    const Json *j;
    switch (op) {
    case 0: return word(o, "when", WHEN, 2, a) && emit(1) && emit(a == 0 ? 0 : 5);
    case 1: {
        if (!word(o, "who", WHO, 7, who) || !word(o, "do", DO, 13, what) || !emit(2)) return false;
        switch (what) {
        case 0: return emit(0) && emit(who) && emit(0);
        case 1: return emit(0) && emit(who) && emit(1);
        case 2: return num(o, "n", 2, 32767, a) && emit(0) && emit(who) && emit(a);
        case 3: return emit(1) && emit(who);
        case 4: return emit(2) && emit(who);
        case 5: return vec(o, "to", 3, false, v) && num(o, "time", 0, MAX_TICKS, t) && emit(3) && emit(who) && emit(v[0]) && emit(v[1]) && emit(v[2]) && emit(t);
        case 6: return vec(o, "to", 3, false, v) && emit(4) && emit(who) && emit(v[0]) && emit(v[1]) && emit(v[2]) && emit(0);
        case 7:
            if (!num(o, "n", 1, 6, a) || !emit(5) || !emit(who) || !emit(a)) return false;
            return a != 4 || (num(o, "distance", -32768, 32767, t) && emit(t));
        case 8: return emit(5) && emit(who) && emit(0);
        case 9: return num(o, "fighter", 0, 9999, a) && emit(6) && emit(who) && emit(a);
        case 10: return num(o, "time", 0, MAX_TICKS, t) && emit(7) && emit(who) && emit(t);
        case 11: return num(o, "time", 0, MAX_TICKS, t) && emit(8) && emit(who) && emit(t);
        default: return emit(9) && emit(who);
        }
    }
    case 2:
        if (!word(o, "mode", M2, 2, what) || !emit(3) || !emit(what == 0 ? 0 : 1)) return false;
        if ((j = o.get("effect")) && j->type == 's' && j->str == "own") a = 9999;
        else if (!num(o, "effect", 0, 9999, a)) return false;
        return emit(a) && (what == 1 || (vec(o, "at", 3, false, v) && emit(v[0]) && emit(v[1]) && emit(v[2])));
    case 3:
        if (!word(o, "mode", MS, 2, what) || !emit(4) || !emit(what)) return false;
        if ((j = o.get("stage")) && j->type == 's' && j->str == "own") a = 0x38;
        else if (!num(o, "stage", -1, 999, a)) return false;
        return emit(a) && (what == 1 || (num(o, "fade_out", 0, MAX_TICKS, t) && emit(t) && num(o, "fade_in", 0, MAX_TICKS, t) && emit(t)));
    case 4:
        if (!num(o, "time", 0, MAX_TICKS, t) || !emit(5) || !emit(t)) return false;
        if (o.get("shot")) return num(o, "shot", 1, 8, a) && emit(0) && emit(a == 8 ? 0 : a);
        if (!emit(1) || !vec(o, "at", 3, false, v) || !vec(o, "look", 3, false, v + 3) || !vec(o, "aim", 3, true, v + 6) || !vec(o, "turn", 3, true, v + 9) ||
            !num(o, "rz", -32768, 32767, v[12], true, 0) || !num(o, "projection", 1, 32767, v[13]))
            return false;
        for (int i = 0; i < 14; i++) emit(v[i]);
        return true;
    case 5:
        if (!word(o, "mode", MEM, 2, what) || !num(o, "model", 0, 9999, a) || !emit(6) || !emit(what == 0 ? 0 : 1) || !emit(a)) return false;
        if (what == 1) return true;
        if (!vec(o, "at", 3, false, v) || !vec(o, "turn", 3, true, v + 3)) return false;
        for (int i = 0; i < 6; i++) emit(v[i]);
        return true;
    case 6: return word(o, "mode", MF, 2, what) && num(o, "time", 0, MAX_TICKS, t) && emit(7) && emit(what) && emit(t);
    case 7: {
        int id, aware = 0;
        std::string name;
        j = o.get("sound");
        if (!j) return fail("no \"sound\"");
        if (j->type == 'n') {
            if (j->num != (double)(int)j->num || j->num < 1 || j->num > 0x59) return fail("\"sound\" is a library name or a number from 1 to 89");
            id = (int)j->num;
        } else if (j->type == 's' && j->str == "hit") id = 0x62;
        else if (j->type == 's' && j->str == "hit_last") id = 0x63;
        else if (j->type == 's') id = 0, name = j->str;
        else return fail("\"sound\" is a library name or a number from 1 to 89");
        if ((j = o.get("hit"))) {
            if (j->type != 'b') return fail("\"hit\" is true or false");
            aware = j->num != 0;
        }
        if (aware && (id == 0x62 || id == 0x63)) return fail("\"hit\": true is for a sound of your own");
        if (!num(o, "time", 0, 3000, t, true, 0)) return false;
        sound = name;
        return emit(10) && emit(id | (aware ? SOUND_HIT_AWARE : 0)) && emit(t);
    }
    case 8: return num(o, "ticks", 0, MAX_TICKS, t) && emit(11) && emit(t);
    case 9: return emit(0);
    default: {
        const Json *ws = o.get("words");
        if (!ws || ws->type != '[' || ws->items.empty() || ws->items.size() > 20) return fail("\"words\" is a list of 1 to 20 numbers");
        for (const auto &it : ws->items) {
            if (it.second.type != 'n' || it.second.num != (double)(int)it.second.num || it.second.num < -32768 || it.second.num > 32767) return fail("\"words\" has a number out of range");
            emit((int)it.second.num);
        }
        int len = script_cmd_len(w.data(), (int)w.size());
        return len == (int)w.size() && len > 0 ? true : fail("\"words\" is not one command of the game's");
    }
    }
}
} // namespace

/* a command's words from its JSON object; the sound name ("hit" aware or not) goes in SOUND */
static bool compile_cmd(const Json &o, ScriptCmd &c, std::string &err) {
    Comp k;
    if (!k.run(o)) return err = k.err, false;
    c.w = k.w, c.sound = k.sound;
    return true;
}

std::string script_to_json(const ScriptWords &s) {
    std::string out = "{\n  \"schema_version\": 1,\n  \"commands\": [";
    for (size_t i = 0; i < s.size(); i++) {
        std::string line = named(s[i]);
        /* what the port would not read back as the same words is written as raw */
        bool same = false;
        if (!line.empty()) {
            const char *p = line.c_str();
            Json j;
            ScriptCmd back;
            std::string e;
            same = json_parse(p, j) && compile_cmd(j, back, e) && back.w == s[i].w && back.sound == s[i].sound;
        }
        if (!same) {
            line = "{\"op\": \"raw\", \"words\": [";
            for (size_t k = 0; k < s[i].w.size(); k++) line += (k ? ", " : "") + std::to_string(s[i].w[k]);
            line += "]}";
        }
        out += (i ? ",\n    " : "\n    ") + line;
    }
    return out + "\n  ]\n}\n";
}

bool script_from_json(const std::string &text, ScriptWords &out, std::string &err) {
    const char *p = text.c_str();
    Json root;
    out.clear();
    if (!json_parse(p, root) || root.type != '{') return err = "not a JSON object", false;
    const Json *ver = root.get("schema_version"), *cmds = root.get("commands");
    if (!ver || ver->type != 'n' || ver->num != 1) return err = "\"schema_version\" is not 1", false;
    if (!cmds || cmds->type != '[' || cmds->items.empty() || (int)cmds->items.size() > MAX_COMMANDS)
        return err = fmt("\"commands\" is a list of 1 to %d commands", MAX_COMMANDS), false;
    size_t words = 0;
    for (size_t i = 0; i < cmds->items.size(); i++) {
        ScriptCmd c;
        std::string e;
        if (!compile_cmd(cmds->items[i].second, c, e)) return err = fmt("command %d: %s", (int)i + 1, e.c_str()), false;
        words += c.w.size();
        out.push_back(c);
    }
    const Json *last = cmds->items.back().second.get("op");
    if (!last || last->type != 's' || last->str != "end") return err = "the last command is not \"end\"", false;
    if ((int)words > MAX_WORDS) return err = fmt("the script is longer than %d words", MAX_WORDS), false;
    return true;
}

/* --- editing --- */

const std::vector<std::string> &script_kinds() {
    static const std::vector<std::string> K = {"Sound: a library sound", "Sound: a game sound", "Sound: the hit's sound", "Wait", "Hit: the other side reacts to the last hit",
                                               "Hit: the other side reacts to the next hit", "Model: a motion", "Model: wait for its motion", "Model: back to idle",
                                               "Sprite effect: the technique's own", "Sprite effect: load the technique's own", "Camera: a shot of the fight", "White flash: start", "White flash: end"};
    return K;
}

ScriptCmd script_new(int kind) {
    ScriptCmd c;
    switch (kind) {
    case 0: c.w = {10, 0, 0}, c.sound = ""; break;
    case 1: c.w = {10, 57, 0}; break;
    case 2: c.w = {10, 0x62, 0}; break;
    case 3: c.w = {11, 10}; break;
    case 4: c.w = {1, 0}; break;
    case 5: c.w = {1, 5}; break;
    case 6: c.w = {2, 0, 0, 2}; break;
    case 7: c.w = {2, 0, 0, 0}; break;
    case 8: c.w = {2, 0, 0, 1}; break;
    case 9: c.w = {3, 0, 9999, 0, 0, 0}; break;
    case 10: c.w = {3, 1, 9999}; break;
    case 11: c.w = {5, 0, 0, 1}; break;
    case 12: c.w = {7, 0, 10}; break;
    default: c.w = {7, 1, 10}; break;
    }
    return c;
}

std::vector<ScriptField> script_fields(const ScriptCmd &c) {
    const std::vector<int> &w = c.w;
    std::vector<ScriptField> f;
    auto add = [&](const char *n, int i, int lo, int hi) { f.push_back({n, i, lo, hi}); };
    auto xyz = [&](const char *a, const char *b, const char *cc, int i) { add(a, i, -32768, 32767), add(b, i + 1, -32768, 32767), add(cc, i + 2, -32768, 32767); };
    switch (w[0]) {
    case 2:
        add("model (0 own, 1-3 partners, 4-6 enemies)", 2, 0, 6);
        switch (w[1]) {
        case 0: add("motion (0 waits for it, 1 idle, 2 on: the motion number)", 3, 0, 32767); break;
        case 3: xyz("x", "y", "z", 3), add("over (ticks, 0 at once)", 6, 0, MAX_TICKS); break;
        case 4: xyz("x", "y", "z", 3); break;
        case 5: if (w[3] == 4) add("distance", 4, -32768, 32767); break;
        case 6: add("fighter", 3, 0, 9999); break;
        case 7: case 8: add("over (ticks, 0 at once)", 3, 0, MAX_TICKS); break;
        }
        break;
    case 3:
        add("effect (9999: the technique's own)", 2, 0, 9999);
        if (w[1] == 0) xyz("x", "y", "z", 3);
        break;
    case 4:
        add("stage (0x38 its own, 0 the battle's, -1 none)", 2, -1, 999);
        if (w[1] == 0) add("fade out (ticks)", 3, 0, MAX_TICKS), add("fade in (ticks)", 4, 0, MAX_TICKS);
        break;
    case 5:
        add("over (ticks, 0 cuts)", 1, 0, MAX_TICKS);
        if (w[2] == 0) add("shot (0 the enemy's view, 1-4 partner's, 5-7 enemy's)", 3, 0, 7);
        else {
            xyz("eye x", "eye y", "eye z", 3), xyz("look x", "look y", "look z", 6), xyz("aim x", "aim y", "aim z", 9), xyz("turn x", "turn y", "turn z", 12);
            add("roll", 15, -32768, 32767), add("projection", 16, 1, 32767);
        }
        break;
    case 6:
        add("effect model", 2, 0, 9999);
        if (w[1] == 0) xyz("x", "y", "z", 3), xyz("turn x", "turn y", "turn z", 6);
        break;
    case 7: add("over (ticks)", 2, 0, MAX_TICKS); break;
    case 10: add("for (ticks, 0 until it ends)", 2, 0, 3000); break;
    case 11: add("ticks", 1, 0, MAX_TICKS); break;
    }
    return f;
}
