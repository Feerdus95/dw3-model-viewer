/* ===== The stage editor's logic, apart from its drawing =====
   What the Stages section does to the data (the parts, the props, the brush's dab), as plain functions with their inputs explicit: the
   section's lambdas (ui/stage_*.inc, ui/edit_actions.inc) call them and keep the history, the flags and the messages. */

/* --- the stage's parts --- */

/* The part copied in stage CLIPSTAGE (CLIP's part 0), put into E, the stage STAGEID: fixed where it stood, with the stage's number as a
   prefix when it came from another. In the same stage it goes a step to the side. The index of the new part. */
static int stage_paste_part(StageEdit &e, const StageEdit &clip, int clipStage, int stageId) {
    char pre[8] = "";
    if (clipStage != stageId) snprintf(pre, sizeof pre, "s%02d_", clipStage);
    e.append(clip, 0, pre);
    if (clipStage == stageId) e.parts.back().move[0] += 300; /* beside the one it copies */
    return (int)e.parts.size() - 1;
}

/* Part K again, a step to the side; its index */
static int stage_duplicate_part(StageEdit &e, int k) {
    e.append(e, k, "");
    e.parts.back().move[0] += 300;
    return (int)e.parts.size() - 1;
}

/* --- the props: a selection (none, one, or a group) and what is done with it --- */

/* The selected props: the group when there is one (more than one), else the one picked if it is in the list */
static std::set<int> props_current(const std::set<int> &group, int picked, int count) {
    std::set<int> g;
    if (group.size() > 1) g = group;
    else if (picked >= 0 && picked < count) g.insert(picked);
    return g;
}

/* G taken out of PROPS; how many went */
static size_t props_remove(std::vector<StageProp> &props, const std::set<int> &g) {
    std::vector<StageProp> keep;
    for (int k = 0; k < (int)props.size(); k++)
        if (!g.count(k)) keep.push_back(props[k]);
    size_t n = props.size() - keep.size();
    props = keep;
    return n;
}

/* G again, a step to the side, at the end of PROPS; the new ones' indices */
static std::set<int> props_duplicate(std::vector<StageProp> &props, const std::set<int> &g) {
    std::set<int> made;
    for (int k : g) {
        StageProp c = props[k];
        c.pos[0] += 300;
        props.push_back(c);
        made.insert((int)props.size() - 1);
    }
    return made;
}

/* What Ctrl+C of props keeps: the props and where their middle was */
struct PropClip {
    std::vector<StageProp> items;
    float center[3] = {0, 0, 0};
};
static void props_copy(PropClip &clip, const std::vector<StageProp> &props, const std::set<int> &g) {
    clip.items.clear();
    float c[3] = {0, 0, 0};
    for (int k : g) clip.items.push_back(props[k]), c[0] += props[k].pos[0], c[1] += props[k].pos[1], c[2] += props[k].pos[2];
    for (int k = 0; k < 3; k++) clip.center[k] = c[k] / g.size();
}

/* The copied props with their middle at AT, at the end of PROPS; their indices */
static std::set<int> props_paste(std::vector<StageProp> &props, const PropClip &clip, const float at[3]) {
    std::set<int> made;
    for (StageProp p : clip.items) {
        for (int k = 0; k < 3; k++) p.pos[k] += at[k] - clip.center[k];
        props.push_back(p);
        made.insert((int)props.size() - 1);
    }
    return made;
}

/* A hash of what the props' tables come from (a prop, its picture or model, the list's leaves): they are rebuilt only when it changes */
static uint64_t props_signature(int stageId, int propGen, const PropsMeta &meta, const std::vector<StageProp> &props) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint32_t w) { h = (h ^ w) * 1099511628211ull, h ^= h >> 29; };
    auto mixf = [&](float f) { uint32_t w; memcpy(&w, &f, 4); mix(w); };
    std::hash<std::string> hs;
    mix((uint32_t)stageId + 7), mix((uint32_t)propGen), mix((uint32_t)meta.leaves), mix((uint32_t)props.size());
    for (const StageProp &p : props) {
        mix((p.emitter ? 1u : 0u) | (p.model ? 2u : 0u) | (p.fixed ? 4u : 0u) | ((uint32_t)p.count << 4));
        for (float f : {p.pos[0], p.pos[1], p.pos[2], p.size[0], p.size[1], p.tint[0], p.tint[1], p.tint[2], p.scale, p.sway, p.rot, p.fog,
                        p.radius, p.top, p.fall, p.wind})
            mixf(f);
        mix((uint32_t)hs(p.image)), mix((uint32_t)hs(p.obj));
    }
    return h;
}

/* --- the brush: dragging on the ground scatters the picked pictures; Ctrl erases their kind --- */

/* What a dab picks from: a picture's name, its size and its weight */
struct BrushPick {
    std::string image;
    float wid, hgt, weight;
};
/* What a dab does: the radius it scatters in, how many pictures, their scale (a range), the tint's variation and the sway */
struct BrushCfg {
    float radius = 600, scale[2] = {0.7f, 1.3f}, var = 0.2f, sway = 0.5f;
    int n = 4;
    bool noRepeat = true; /* not the picture just before it, when there are others */
};
struct DabResult {
    bool changed = false; /* props were added, or (erasing) removed */
    bool capped = false;  /* adding stopped at MAXBB billboards */
};

/* One dab at G on the ground, into PROPS. Erasing takes out the billboards of the pool's pictures within the radius (emitters and models
   stay). Adding scatters CFG.n pictures, drawn by weight from POOL (LASTPICK: the one before), up to MAXBB billboards in all. */
static DabResult brush_dab_props(std::vector<StageProp> &props, const std::vector<BrushPick> &pool, int &lastPick, const BrushCfg &cfg, int maxBB,
                                 const float g[3], bool erase) {
    DabResult r;
    if (erase) {
        size_t before = props.size();
        props.erase(std::remove_if(props.begin(), props.end(),
                                   [&](const StageProp &p) {
                                       float dx = p.pos[0] - g[0], dz = -p.pos[2] - g[2];
                                       if (p.emitter || p.model || dx * dx + dz * dz > cfg.radius * cfg.radius) return false;
                                       for (const BrushPick &k : pool)
                                           if (k.image == p.image) return true;
                                       return false;
                                   }),
                    props.end());
        r.changed = props.size() != before;
        return r;
    }
    int bb = 0;
    for (const StageProp &p : props) bb += !p.emitter && !p.model;
    for (int k = 0; k < cfg.n && bb < maxBB; k++, bb++) {
        /* a picture by its weight; not the one just before it when there are others */
        bool skip = cfg.noRepeat && pool.size() > 1;
        float tot = 0;
        for (int j = 0; j < (int)pool.size(); j++)
            if (!(skip && j == lastPick)) tot += pool[j].weight;
        float want = psim_rnd() * tot;
        int pick = 0;
        for (int j = 0; j < (int)pool.size(); j++) {
            if (skip && j == lastPick) continue;
            pick = j, want -= pool[j].weight;
            if (want <= 0) break;
        }
        lastPick = pick;
        StageProp s;
        s.image = pool[pick].image, s.size[0] = pool[pick].wid, s.size[1] = pool[pick].hgt, s.sway = cfg.sway;
        float a = psim_rnd() * 6.2831853f, rad = sqrtf(psim_rnd()) * cfg.radius, tv = 1 - cfg.var * psim_rnd();
        s.pos[0] = g[0] + cosf(a) * rad, s.pos[1] = 0, s.pos[2] = -(g[2] + sinf(a) * rad);
        s.scale = cfg.scale[0] + psim_rnd() * (cfg.scale[1] - cfg.scale[0]);
        for (float &c : s.tint) c = roundf(255 * tv);
        props.push_back(s);
        r.changed = true;
    }
    r.capped = bb >= maxBB;
    return r;
}

/* The pool's sizes for a stroke: one picture is 300 tall; with Mix the tallest is 300 and the others keep their proportion to it (reeds
   taller than a tuft). TALL is the tallest picture's height. */
static void brush_normalize(std::vector<BrushPick> &pool, bool mix, float tall) {
    for (BrushPick &k : pool) {
        float hg = mix ? 300 * k.hgt / tall : 300;
        k.wid = std::max(10.0f, hg * k.wid / k.hgt), k.hgt = hg;
    }
}
