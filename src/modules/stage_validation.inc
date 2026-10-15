/* --stages: each stage read, built back (and through an OBJ) and read
   again; what changed of its polygons and their corners' texels */
static int check_stages() {
    std::vector<uint8_t> table;
    read_file(fid_path(459), table);
    int bad = 0;
    auto corners = [](const Model &m) {
        std::vector<uint32_t> out;
        for (size_t b = 1; b < m.bones.size(); b++)
            for (const Poly &p : m.meshes[m.bones[b].mesh].polys)
                for (int k = 0; k < (p.quad ? 4 : 3); k++) {
                    uint16_t t = corner_texel(m, p, k);
                    out.push_back((uint32_t)b << 17 | (p.abr ? t : (t & 0x7FFF) | (t ? 0 : 0x10000)));
                }
        std::sort(out.begin(), out.end());
        return out;
    };
    for (const StageInfo &si : read_stage_infos(table)) {
        std::vector<uint8_t> disc, built, again;
        read_file(fid_path(si.fid), disc);
        Model m, m2, m3;
        if (!m.load("", disc, nullptr, si.boneEntry)) {
            printf("stage %2d: %s\n", si.id, m.error.c_str());
            bad++;
            continue;
        }
        StageEdit e;
        e.fromModel(m, si);
        StageInfo info = si, info2 = si;
        StageBuild made, made2;
        if (!e.build(disc, si.boneEntry, built, info, made) || !m2.load("", built, nullptr, si.boneEntry)) {
            printf("stage %2d: %s%s\n", si.id, e.error.c_str(), m2.error.c_str());
            bad++;
            continue;
        }
        std::string dir = g_exe_dir + "stages/check";
        StageEdit e2 = e;
        bool obj = e.exportObj(dir, "check") && e2.importObj(dir + "/check.obj") &&
                   e2.build(disc, si.boneEntry, again, info2, made2) && m3.load("", again, nullptr, si.boneEntry);
        std::vector<uint32_t> a = corners(m), b = corners(m2), c = obj ? corners(m3) : std::vector<uint32_t>();
        int same = 0, same3 = 0;
        for (size_t i = 0; i < std::min(a.size(), b.size()); i++) same += a[i] == b[i];
        for (size_t i = 0; i < std::min(a.size(), c.size()); i++) same3 += a[i] == c[i];
        bool ok = a.size() == b.size() && obj && a.size() == c.size() && memcmp(info.noBounds, si.noBounds, 8) == 0;
        printf("stage %2d: %2d bones, %4d corners: built %4d same, OBJ %4d same; %3d pieces%s, %6d bytes%s\n", si.id,
               (int)m.bones.size() - 1, (int)a.size(), same, same3, made.islands,
               made.shrink < 1 ? " (shrunk)" : "", (int)built.size(), ok ? "" : "  BAD");
        bad += !ok;
    }
    /* harder: stage 13 with all of 8's and 24's parts, and with its images changed (new texels, 16 colors a piece) */
    std::vector<StageInfo> infos = read_stage_infos(table);
    if (infos.size() > 24) {
        StageEdit e[3];
        for (int k = 0; k < 3; k++) {
            std::vector<uint8_t> f;
            Model m;
            const StageInfo &si = infos[k == 0 ? 13 : k == 1 ? 8 : 24];
            read_file(fid_path(si.fid), f);
            if (m.load("", f, nullptr, si.boneEntry)) e[k].fromModel(m, si);
        }
        StageEdit mixed = e[0], edited = e[0];
        for (int k = 1; k < 3; k++)
            for (int p = 0; p < (int)e[k].parts.size(); p++) mixed.append(e[k], p, "x_");
        for (StageImage &im : edited.images) {
            im.source = -1;
            for (uint32_t &c : im.px) c ^= 0x00102030u;
        }
        std::vector<uint8_t> disc, out;
        read_file(fid_path(infos[13].fid), disc);
        for (StageEdit *t : {&mixed, &edited}) {
            StageInfo info = infos[13];
            StageBuild made;
            Model m;
            bool ok = t->build(disc, info.boneEntry, out, info, made) && m.load("", out, nullptr, info.boneEntry);
            printf("stage 13 %s: %d parts -> %d bones, %d pieces, shrunk to %.2f, %d bytes %s%s\n", t == &mixed ? "+ 8 + 24" : "edited",
                   (int)t->parts.size(), ok ? (int)m.bones.size() - 1 : 0, made.islands, made.shrink, (int)out.size(),
                   ok ? "" : "BAD ", t->error.c_str());
            bad += !ok;
        }
    }
    return bad != 0;
}

