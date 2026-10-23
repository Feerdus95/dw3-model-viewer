    auto browser = [&]() {
        static const char *TITLE[SECTIONS] = {"Models", "Stages", "Bones", "Motions", "Action scripts", "Library", "Effects", "Texture pages", "Digimon World 2"};
        panel_title(SECTION_ICON[section], TITLE[section]);
        switch (section) {
        case FILES: files_browser(); break;
        case STAGES: stages_browser(); break;
        case MODEL: model_browser(); break;
        case MOTIONS: draw_motions_browser(app); break;
        case FIGHTER: fighter_browser(); break;
        case CAMERAS: cameras_browser(); break;
        case EFFECTS: effects_browser(); break;
        case TEXTURES: draw_textures_browser(app); break;
        case DW2: dw2_browser(); break;
        }
    };
    auto inspector = [&]() {
        static const char *TITLE[SECTIONS] = {"Details", "Stage editor", "Model", "Playback", "Fighter", "Workbench", "Effect", "Texture", "Digimon World 2"};
        panel_title(IC_INFO, TITLE[section]);
        switch (section) {
        case FILES: files_inspector(); break;
        case STAGES: stages_inspector(); break;
        case MODEL: model_inspector(); break;
        case MOTIONS: draw_motions_inspector(app); break;
        case FIGHTER: fighter_inspector(); break;
        case CAMERAS: cameras_inspector(); break;
        case EFFECTS: effects_inspector(); break;
        case TEXTURES: draw_textures_inspector(app); break;
        case DW2: dw2_inspector(); break;
        }
    };

