/* ===== --test-stage: stage_logic.inc, headless (a synthetic stage and props: no disc, no window) ===== */
static int stage_selftest() {
    int bad = 0;
    auto ok = [&](bool c, const char *what) { printf("%s  %s\n", c ? "ok  " : "FAIL", what), bad += !c; };
    auto prop = [](const char *image, float x, float z) {
        StageProp p;
        p.image = image, p.pos[0] = x, p.pos[2] = z;
        return p;
    };

    /* the selection */
    std::set<int> none, group = {0, 2};
    ok(props_current(none, -1, 3).empty() && props_current(none, 1, 3) == std::set<int>({1}) && props_current(none, 3, 3).empty() && props_current(none, -1, 0).empty(),
       "the selected props: the one picked if it is in the list, else none");
    ok(props_current(group, 1, 3) == group && props_current(std::set<int>({1}), 2, 3) == std::set<int>({2}), "a group of more than one wins; a group of one is only the one picked");

    /* remove, duplicate, copy, paste */
    std::vector<StageProp> list = {prop("a", 0, 0), prop("b", 100, 0), prop("c", 200, 0), prop("d", 300, 0)};
    std::vector<StageProp> before = list;
    ok(props_remove(list, {1, 3}) == 2 && list.size() == 2 && list[0].image == "a" && list[1].image == "c", "removing a group takes those and keeps the order of the rest");
    ok(props_remove(list, {}) == 0 && list.size() == 2, "an empty group removes nothing");
    list = before;
    std::set<int> made = props_duplicate(list, {0, 2});
    ok(list.size() == 6 && made == std::set<int>({4, 5}) && list[4].image == "a" && list[4].pos[0] == 300 && list[5].image == "c" && list[5].pos[0] == 500 && list[0].pos[0] == 0 &&
           list[2].pos[0] == 200,
       "duplicating a group adds copies a step to the side, at the end, and leaves the originals");
    list = before;
    PropClip clip;
    props_copy(clip, list, {0, 2});
    ok(clip.items.size() == 2 && clip.center[0] == 100 && clip.center[2] == 0, "copying keeps the props and where their middle was");
    float at[3] = {1000, 0, 50};
    made = props_paste(list, clip, at);
    ok(made == std::set<int>({4, 5}) && list[4].pos[0] == 900 && list[5].pos[0] == 1100 && list[4].pos[2] == 50, "pasting puts their middle at the asked place and keeps how they were around it");
    ok(list.size() == 6 && list[0].pos[0] == 0 && clip.items[0].pos[0] == 0, "and leaves the list's own and the clipboard as they were");

    /* the signature of what the tables come from */
    PropsMeta meta;
    uint64_t h = props_signature(12, 0, meta, list);
    ok(h == props_signature(12, 0, meta, list), "the same props give the same signature");
    std::vector<StageProp> l2 = list;
    l2[1].pos[0] += 1;
    ok(props_signature(12, 0, meta, l2) != h, "a prop's position changes it");
    l2 = list, l2[1].image = "z";
    ok(props_signature(12, 0, meta, l2) != h, "so does its picture");
    l2 = list, l2[1].emitter = true;
    ok(props_signature(12, 0, meta, l2) != h, "its kind");
    l2 = list, l2[1].count += 1;
    ok(props_signature(12, 0, meta, l2) != h, "its count");
    l2 = list, l2.pop_back();
    ok(props_signature(12, 0, meta, l2) != h, "the number of props");
    meta.leaves = 100;
    ok(props_signature(12, 0, meta, list) != h, "the leaves");
    meta = PropsMeta();
    ok(props_signature(13, 0, meta, list) != h && props_signature(12, 1, meta, list) != h, "the stage and the generation of the files");

    /* the stage's parts */
    StageEdit st;
    StagePart ground;
    ground.name = "ground";
    for (int i = 0; i < 16; i++) ground.bind[i] = i % 5 == 0 ? 1.0f : 0.0f;
    ground.verts = {{0, 0, 0}, {100, 0, 0}, {0, 0, 100}};
    st.parts.push_back(ground);
    StageEdit clipPart;
    clipPart.append(st, 0, "");
    StageEdit probe = st;
    probe.append(clipPart, 0, "");
    float plain = probe.parts.back().move[0];
    int k = stage_paste_part(st, clipPart, 12, 12);
    ok(k == 1 && st.parts.size() == 2 && st.parts[1].name == "ground_2" && st.parts[1].move[0] == plain + 300, "pasting in the stage it came from puts it a step to the side, with a free name");
    k = stage_paste_part(st, clipPart, 3, 12);
    ok(k == 2 && st.parts[2].name == "s03_ground" && st.parts[2].move[0] == plain, "from another stage it keeps its place and takes that stage's number in its name");
    k = stage_duplicate_part(st, 0);
    ok(k == 3 && st.parts.size() == 4 && st.parts[3].move[0] == plain + 300 && st.parts[3].name != st.parts[0].name, "duplicating adds a step to the side with another name");

    /* the brush */
    std::vector<StageProp> p;
    std::vector<BrushPick> pool = {{"grass", 100, 200, 1}, {"reed", 50, 300, 1}};
    BrushCfg cfg;
    int lastPick = -1;
    float g[3] = {1000, 0, 500};
    DabResult r = brush_dab_props(p, pool, lastPick, cfg, 20000, g, false);
    bool inside = true, known = true, tinted = true, scaled = true;
    for (const StageProp &s : p) {
        float dx = s.pos[0] - g[0], dz = -s.pos[2] - g[2];
        inside = inside && dx * dx + dz * dz <= cfg.radius * cfg.radius + 1;
        known = known && (s.image == "grass" || s.image == "reed") && s.sway == cfg.sway;
        tinted = tinted && s.tint[0] == s.tint[1] && s.tint[1] == s.tint[2] && s.tint[0] <= 255 && s.tint[0] >= roundf(255 * (1 - cfg.var));
        scaled = scaled && s.scale >= cfg.scale[0] && s.scale <= cfg.scale[1];
    }
    ok(r.changed && !r.capped && p.size() == 4 && inside && known && tinted && scaled, "a dab adds its pictures inside the radius, from the pool, with the sway, a tint in range and a scale in range");
    cfg.n = 9;
    p.clear(), lastPick = -1;
    brush_dab_props(p, pool, lastPick, cfg, 20000, g, false);
    bool alternate = true;
    for (size_t i = 1; i < p.size(); i++) alternate = alternate && p[i].image != p[i - 1].image;
    ok(p.size() == 9 && alternate, "with two pictures and no repeat, no picture follows itself");
    cfg.n = 20, cfg.noRepeat = false;
    std::vector<BrushPick> weighted = {{"grass", 100, 200, 1}, {"reed", 50, 300, 0}};
    p.clear(), lastPick = -1;
    brush_dab_props(p, weighted, lastPick, cfg, 20000, g, false);
    bool onlyGrass = true;
    for (const StageProp &s : p) onlyGrass = onlyGrass && s.image == "grass";
    ok(p.size() == 20 && onlyGrass, "a picture with no weight is never picked");
    cfg = BrushCfg(), cfg.n = 4;
    p.clear();
    brush_dab_props(p, pool, lastPick, cfg, 6, g, false);
    r = brush_dab_props(p, pool, lastPick, cfg, 6, g, false);
    ok(p.size() == 6 && r.capped, "the dabs stop at the most the viewer draws, and say so");
    p.push_back(prop("grass", 5000, 5000)); /* far away */
    StageProp emitter = prop("grass", g[0], -g[2]);
    emitter.emitter = true;
    StageProp model = prop("grass", g[0], -g[2]);
    model.model = true;
    StageProp other = prop("tree", g[0], -g[2]);
    p.push_back(emitter), p.push_back(model), p.push_back(other);
    size_t n0 = p.size();
    r = brush_dab_props(p, pool, lastPick, cfg, 20000, g, true);
    bool left = true;
    for (const StageProp &s : p) left = left && !(!s.emitter && !s.model && (s.image == "grass" || s.image == "reed") && fabsf(s.pos[0] - g[0]) < 100 && fabsf(-s.pos[2] - g[2]) < 100);
    ok(r.changed && !r.capped && p.size() < n0 && left, "erasing takes the pool's billboards near the dab");
    bool kept = false, keptEmitter = false, keptModel = false, keptOther = false, keptFar = false;
    for (const StageProp &s : p) {
        keptOther = keptOther || s.image == "tree";
        keptEmitter = keptEmitter || s.emitter;
        keptModel = keptModel || s.model;
        keptFar = keptFar || s.pos[0] == 5000;
    }
    kept = keptEmitter && keptModel && keptOther && keptFar;
    ok(kept, "emitters, models, other pictures and far ones stay");
    r = brush_dab_props(p, pool, lastPick, cfg, 20000, g, true);
    ok(!r.changed, "erasing where there is nothing changes nothing");
    std::vector<BrushPick> sizes = {{"tall", 100, 600, 1}, {"short", 100, 300, 1}};
    brush_normalize(sizes, true, 600);
    ok(sizes[0].hgt == 300 && sizes[0].wid == 50 && sizes[1].hgt == 150 && sizes[1].wid == 50, "with Mix the tallest picture is 300 and the others keep their proportion to it");
    sizes = {{"tall", 100, 600, 1}, {"short", 100, 300, 1}};
    brush_normalize(sizes, false, 600);
    ok(sizes[0].hgt == 300 && sizes[1].hgt == 300 && sizes[1].wid == 100, "alone, a picture is 300 tall");
    BrushState fresh; /* what the brush was before it was one struct */
    ok(!fresh.on && !fresh.stroke && !fresh.added && !fresh.mixOn && fresh.cfg.radius == 600 && fresh.cfg.scale[0] == 0.7f && fresh.cfg.scale[1] == 1.3f && fresh.cfg.var == 0.2f &&
           fresh.cfg.sway == 0.5f && fresh.cfg.n == 4 && fresh.cfg.noRepeat && fresh.lastPick == -1 && fresh.maxBB == 20000 && fresh.pool.empty() && fresh.mix.empty(),
       "a new brush starts as it always did");
    printf("%s\n", bad ? "FAILED" : "all hold");
    return bad != 0;
}
