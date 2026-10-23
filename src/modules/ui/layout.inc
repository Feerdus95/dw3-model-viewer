    auto browser = [&]() {
        static const char *TITLE[SECTIONS] = {"Models", "Stages", "Bones", "Motions", "Action scripts", "Effects", "Texture pages", "Digimon World 2"};
        panel_title(SECTION_ICON[section], TITLE[section]);
        switch (section) {
        case FILES: files_browser(); break;
        case STAGES: stages_browser(); break;
        case MODEL: model_browser(); break;
        case MOTIONS: motions_browser(); break;
        case FIGHTER: fighter_browser(); break;
        case EFFECTS: effects_browser(); break;
        case TEXTURES: textures_browser(); break;
        case DW2: dw2_browser(); break;
        }
    };
    auto inspector = [&]() {
        static const char *TITLE[SECTIONS] = {"Details", "Stage editor", "Model", "Playback", "Fighter", "Effect", "Texture", "Digimon World 2"};
        panel_title(IC_INFO, TITLE[section]);
        switch (section) {
        case FILES: files_inspector(); break;
        case STAGES: stages_inspector(); break;
        case MODEL: model_inspector(); break;
        case MOTIONS: motions_inspector(); break;
        case FIGHTER: fighter_inspector(); break;
        case EFFECTS: effects_inspector(); break;
        case TEXTURES: textures_inspector(); break;
        case DW2: dw2_inspector(); break;
        }
    };

