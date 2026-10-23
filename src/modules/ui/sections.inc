    /* ===== the UI: sections, selection, editing, and each section's two
       panels (the browser on the left, the inspector on the right) ===== */

    enum { FILES, STAGES, MODEL, MOTIONS, FIGHTER, CAMERAS, EFFECTS, TEXTURES, DW2, SECTIONS };
    static const char *SECTION_NAME[SECTIONS] = {"Files", "Stages", "Model", "Motions", "Fighter", "Cameras", "Effects", "Textures", "DW2"};
    static const char *SECTION_ICON[SECTIONS] = {IC_FILES, IC_STAGE, IC_MODEL, IC_MOTION, IC_FIGHTER, IC_CAMERA, IC_EFFECT, IC_TEXTURE, IC_FILES};
    static const char *SECTION_TIP[SECTIONS] = {
        "The game's models: Digimon, enemies and technique models",
        "The 57 fight stages: move, add and remove their parts, save them for the native port",
        "The open model's bones and how it's drawn",
        "The open model's animations",
        "Who uses the open model: cameras and action scripts",
        "Candidate cameras, Fit and choreographies for the open fighter's scripts, kept as presets and applications",
        "The battle's 2D effects, and their own images for the native port",
        "The open model's texture pages, palettes and UVs",
        "Digimon World 2: its Digimon and effect models, from the folder dw2-extractor made"};
    auto needs_model = [](int s) { return s == MODEL || s == MOTIONS || s == FIGHTER || s == CAMERAS || s == TEXTURES; };
