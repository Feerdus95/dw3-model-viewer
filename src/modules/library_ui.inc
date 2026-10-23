/* ===== Shared pieces of the libraries (presets.json, applications.json) =====
   What the two lists do the same way: the save form, replacing one with the same id after asking, deleting after asking, and the
   import/export buttons with the file dialogs. Plain functions: the Fighter section's panels call them with their own lists. */

struct Picked { /* what the file dialogs gave, maybe from another thread */
    std::mutex m;
    std::vector<std::string> files;
    std::string folder, data, obj, bin, prop, propTex, dw2, presetIn, presetOut, appIn, appOut; /* a presets or applications file to read, or to write */
};

/* the question before a change that can't be undone (drawn as a modal by the app loop) */
struct Ask {
    std::string title, text, ok;
    std::function<void()> then;
    bool open = false, danger = true;
};

/* the status combo's index for a saved status ("candidate", "approved", "rejected") */
static int status_index(const std::string &s) { return s == PRESET_STATUS[1] ? 1 : s == PRESET_STATUS[2] ? 2 : 0; }

/* What the Library shows, for both lists: a context, an origin (where the fighter comes from; only with a mod's context loaded) and a
   status. Empty: all */
struct LibFilter {
    std::string ctx, origin, status;
};
/* Does an entry of CONTEXT and STATUS, of a fighter whose origin is ORIGIN, pass? ORIGIN is only looked at while MODDED and the filter has one */
static bool lib_pass(const LibFilter &f, bool modded, const std::string &context, const std::string &status, const std::string &origin) {
    if (!f.ctx.empty() && context != f.ctx) return false;
    if (!f.status.empty() && status != f.status) return false;
    if (modded && !f.origin.empty() && origin != f.origin) return false;
    return true;
}

/* The one form both Save buttons use: Name, Variant (applications only), Notes and Status. Loading an entry fills it */
struct LibForm {
    char name[64] = "", notes[256] = "", variant[24] = "";
    int status = 0;
    void load(const Preset &p) {
        snprintf(name, sizeof name, "%s", p.name.c_str());
        snprintf(notes, sizeof notes, "%s", p.notes.c_str());
        snprintf(variant, sizeof variant, "%s", "");
        status = status_index(p.status);
    }
    void load(const Application &a) {
        snprintf(name, sizeof name, "%s", a.name.c_str());
        snprintf(notes, sizeof notes, "%s", a.notes.c_str());
        snprintf(variant, sizeof variant, "%s", a.variant.c_str());
        status = status_index(a.status);
    }
    const char *status_name() const { return PRESET_STATUS[std::clamp(status, 0, 2)]; }
};
static void lib_form(LibForm &form) {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##lname", "Name", form.name, sizeof form.name);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##lvar", "Variant (applications: to keep several for this script)", form.variant, sizeof form.variant);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##lnotes", "Notes", form.notes, sizeof form.notes);
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##lstatus", &form.status, "Candidate\0Approved\0Rejected\0");
}

/* The rows of a list (presets or applications) the same way: a selectable line with its tooltip and a context menu. SHOWN picks the rows,
   LABEL is the text, USABLE whether a click loads it, TIPTEXT the tooltip, MENU draws the menu's items (it gets a copy: an item may change
   the list). Returns how many rows are shown. */
template <class T>
static int library_rows(std::vector<T> &list, const std::string &active, int idBase, const std::function<bool(const T &)> &shown,
                        const std::function<std::string(const T &)> &label, const std::function<bool(const T &)> &usable,
                        const std::function<std::string(const T &)> &tipText, const std::function<void(T &)> &load, const std::function<void(T)> &menu) {
    int n = 0;
    for (int i = 0; i < (int)list.size(); i++) {
        if (!shown(list[i])) continue;
        n++;
        ImGui::PushID(idBase + i);
        if (ImGui::Selectable((label(list[i]) + "###row").c_str(), active == list[i].id()) && usable(list[i])) load(list[i]);
        tip("%s", tipText(list[i]).c_str());
        if (ImGui::BeginPopupContextItem()) {
            menu(list[i]);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    return n;
}

/* Puts ITEM in LIST: when one with its id is there, asks first (TEXT says what is being replaced) and replaces it; else adds it.
   DONE runs after it is in. The list and what DONE touches must outlive the question (they are main's locals). */
template <class T>
static void keep_or_ask(Ask &ask, std::vector<T> &list, const T &item, const char *title, const std::function<std::string(const T &)> &text, const std::function<void(const T &)> &done) {
    auto it = std::find_if(list.begin(), list.end(), [&](const T &o) { return o.id() == item.id(); });
    auto put = [&list, item, done, idx = (int)(it - list.begin()), has = it != list.end()]() {
        if (has && idx < (int)list.size()) list[idx] = item;
        else list.push_back(item);
        done(item);
    };
    if (it == list.end()) return put();
    ask.title = title, ask.text = text(*it);
    ask.ok = "Replace", ask.danger = true, ask.open = true, ask.then = put;
}

/* Asks, then deletes the entry with ID from LIST (ACTIVE forgets it when it was the active one), and PERSIST writes the file */
template <class T>
static void ask_delete(Ask &ask, std::vector<T> &list, std::string &active, const std::string &id, const char *title, const std::string &text, std::function<void()> persist) {
    ask.title = title, ask.text = text, ask.ok = "Delete", ask.danger = true, ask.open = true;
    ask.then = [&list, &active, id, persist]() {
        list.erase(std::remove_if(list.begin(), list.end(), [&](const T &o) { return o.id() == id; }), list.end());
        if (active == id) active.clear();
        persist();
    };
}

/* The context menu's "Delete the NOUN": asks (EXTRA says more about it), then deletes */
template <class T>
static void menu_delete(Ask &ask, std::vector<T> &list, std::string &active, const T &e, const std::string &noun, const std::string &extra, std::function<void()> persist) {
    if (ImGui::MenuItem((std::string(IC_DELETE "  Delete the ") + noun).c_str()))
        ask_delete(ask, list, active, e.id(), ("Delete the " + noun).c_str(), "Delete \"" + e.name + "\"? It can't be undone" + extra + ".", persist);
}

/* The file dialogs' answers go to the Picked field they were opened for */
template <std::string Picked::*Field>
static void picked_cb(void *u, const char *const *list, int) {
    if (!list || !*list) return;
    Picked *p = (Picked *)u;
    std::lock_guard<std::mutex> lock(p->m);
    p->*Field = *list;
}

/* The Import and Export buttons of a library file. The dialogs answer into IN and OUT of PICKED (take_picked reads them) */
template <std::string Picked::*In, std::string Picked::*Out>
static void json_buttons(Picked &picked, SDL_Window *win, const char *importLabel, const char *exportLabel, const char *filterName, const char *defaultFile) {
    static const SDL_DialogFileFilter FILTER = {filterName, "json"};
    if (button(importLabel)) SDL_ShowOpenFileDialog(picked_cb<In>, &picked, win, &FILTER, 1, nullptr, false);
    ImGui::SameLine();
    if (button(exportLabel)) SDL_ShowSaveFileDialog(picked_cb<Out>, &picked, win, &FILTER, 1, defaultFile);
}

/* what a dialog answered (and forgets it) */
static std::string take_picked(Picked &picked, std::string Picked::*field) {
    std::string out;
    std::lock_guard<std::mutex> lock(picked.m);
    out.swap(picked.*field);
    return out;
}

/* --test-library: the helpers on a list of presets, headless */
static int library_selftest() {
    int bad = 0;
    auto ok = [&](bool c, const char *what) { printf("%s  %s\n", c ? "ok  " : "FAIL", what), bad += !c; };
    auto mk = [](int script, const char *name) {
        Preset p;
        p.fighter = 1, p.scriptIdx = script, p.name = name;
        return p;
    };
    Ask ask;
    std::vector<Preset> list;
    std::string active;
    int persisted = 0;
    auto done = [&](const Preset &p) { active = p.id(), persisted++; };
    auto text = [](const Preset &o) { return "replace " + o.name; };
    keep_or_ask<Preset>(ask, list, mk(1, "a"), "Replace", text, done);
    ok(list.size() == 1 && persisted == 1 && !ask.open && active == list[0].id(), "a new one is added, with no question");
    keep_or_ask<Preset>(ask, list, mk(2, "b"), "Replace", text, done);
    ok(list.size() == 2 && persisted == 2 && !ask.open, "another id is another entry");
    keep_or_ask<Preset>(ask, list, mk(1, "a2"), "Replace", text, done);
    ok(list.size() == 2 && list[0].name == "a" && persisted == 2 && ask.open && ask.title == "Replace" && ask.text == "replace a" && ask.ok == "Replace" && ask.danger,
       "the same id asks first, naming what is there, and changes nothing yet");
    ask.then();
    ok(list.size() == 2 && list[0].name == "a2" && persisted == 3 && active == list[0].id(), "confirmed, it replaces in place and persists");
    std::string id1 = list[0].id();
    ask_delete(ask, list, active, id1, "Delete", "sure?", [&] { persisted++; });
    ok(list.size() == 2 && ask.title == "Delete" && ask.text == "sure?" && ask.ok == "Delete" && ask.danger, "delete asks first and changes nothing yet");
    ask.then();
    ok(list.size() == 1 && list[0].name == "b" && active.empty() && persisted == 4, "confirmed, it deletes, forgets the active one and persists");
    active = list[0].id();
    ask_delete(ask, list, active, "preset:nothing", "Delete", "x", [&] { persisted++; });
    ask.then();
    ok(list.size() == 1 && active == list[0].id(), "an id that is not there deletes nothing and keeps the active one");
    ok(status_index("candidate") == 0 && status_index("approved") == 1 && status_index("rejected") == 2 && status_index("other") == 0, "the status combo's index");
    Picked pk;
    pk.presetIn = "x.json";
    ok(take_picked(pk, &Picked::presetIn) == "x.json" && pk.presetIn.empty() && take_picked(pk, &Picked::presetIn).empty(), "a dialog's answer is taken once");
    char *none[1] = {nullptr};
    picked_cb<&Picked::appOut>(&pk, nullptr, 0);
    picked_cb<&Picked::appOut>(&pk, none, 0);
    const char *got[2] = {"y.json", nullptr};
    picked_cb<&Picked::appOut>(&pk, got, 0);
    ok(pk.appOut == "y.json" && pk.appIn.empty(), "a dialog's answer lands in its own field; a cancelled one in none");
    printf("%s\n", bad ? "FAILED" : "all hold");
    return bad != 0;
}
