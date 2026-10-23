    /* ===== Fighter: what a script is, and playing it ===== */
    /* What each script's number is: its technique's script (TechData.script)
       or the reaction the other side runs for a hit's result. The ones with no
       name are listed by the techniques that use them, or not known */
    struct ScriptRole {
        const char *name, *examples;
        int group;
    };
    static const char *SCRIPT_GROUPS[4] = {"Reactions", "Attacks and techniques", "Support and field", "Other"};
    auto script_role = [](int i) -> ScriptRole {
        switch (i) {
        case 2: return {"Hit reaction", "Runs when the other side's attack hits", 0};
        case 3: return {"KO reaction", "Runs when the other side's attack knocks it out", 0};
        case 4: return {"Evade reaction", "Runs when the other side's attack misses", 0};
        case 5: return {"Basic attack (Fight)", "The Fight command, and many enemy attacks", 1};
        case 6: return {"Physical technique", "Heat Cutter, Whirlwind, Spinal Tap...", 1};
        case 8: return {"", "Used by Hammer Rush, Impact Rush", 1};
        case 9: return {"Magic technique", "Flame Ball, Divine Rain, Ice Blow...", 1};
        case 11: return {"Signature technique", "Hot Head, Pepper Breath, Nova Blast...", 1};
        case 12: return {"DNA technique", "Omnimon, Beelzemon, BladeGarurumon...", 1};
        case 13: return {"", "Used by Triple Fire, Giga Fire, Black Thorn...", 1};
        case 10: return {"Support technique", "Small Heal, Antidote, Erase Poison...", 2};
        case 15: return {"Field technique", "Fire Field, Water Field, Dark Field...", 2};
        case 16: return {"", "Used by Armor Break, Slow Down, Mega Break", 2};
        default: return {"", "", 3};
        }
    };

    /* A script played as the battle runs it: motions, waits, cameras and the
       sprite effects it starts, which draw over the view where the game
       would put them. It leaves out what the viewer has no model for: the
       jumps and moves, the other side, the sounds, fades and added models */
    struct FxShot {
        int id;
        float t, pos[3];
        int screen = 0; /* the point is a screen pixel: 1 in front of everything, 2 behind it */
    };
    /* the sprites and models the view draws this frame */
    std::vector<SpriteDraw> spriteDraws;
    std::vector<ExtraModel> extraModels;
    /* the front layer's effects (FIGHTSTG_updateSpriteEffect) draw over the models; the rest sort with them */
    auto fx_front = [](int id) { return (id >= 1000 && id < 1003) || id == 1007; };
    struct ScriptRun {
        bool on = false;
        int script = 0, pc = 0;
        float wait = 0;
        bool waitMotion = false;
        /* the game's camera the script has set (or the battle's default view), kept while it plays */
        bool camSet = false;
        Camera cam;
        float fov = 0.8f;
        int camIdx = -1;
        /* the battle camera twice, on the same script: A with the cameras the script asks for, B with the candidate in their place */
        CamTrack a, b;
        float camAcc = 0;
        bool rollNoted = false;
        int camOrd = 0; /* the camera commands met so far */
    } run;
    bool lockCam = true;
    /* A candidate camera (Camera workbench): another fighter's camera, adjusted, in the place of the camera the script asks for, at the
       same moments. FIGHTER is its donor in g_game.fighters (same side as the model); EVERY: also at the camera commands the
       player doesn't act on (explicit ones, the other side's shots). ORIG: the game's camera the script last asked for (-1 the default view) */
    struct Candidate {
        bool on = false, every = false;
        int fighter = -1, cam = 0;
        GameCamera c = {};
        int orig = -1;
        int script = -1;             /* the script last played, of OF: what a preset is made for */
        const Fighter *of = nullptr;
        GameCamera before = {};      /* the camera before a fit, for undoing it */
        double fitK = 0;             /* the scale of the fit the camera started from; 0 none */
    } cand;
    /* the presets (presets.json), read when first wanted */
    struct PresetUi {
        bool loaded = false, blocked = false;
        std::string error, msg, active, inFile;
        std::vector<Preset> list;
        char name[64] = "", notes[256] = "";
        int status = 0;
        std::string ctx;    /* the list shows only this context's presets; empty all */
        std::string origin; /* only the fighters of this origin (donors and, by the preset's fighter, presets); empty all */
        struct In {
            Preset p;
            bool dup, take;
        };
        std::vector<In> in;
    } pre;
    /* The choreography (templates.inc): APP, an application being worked on, its EFF effective cameras on the target's script, and the
       saved ones. ON: B is the application instead of the candidate camera */
    struct ChoreoUi {
        bool on = false, has = false, fit = true;
        Application app;
        std::vector<EffShot> eff;
        std::vector<std::string> notes;
        FitFrame ff;
        std::pair<int, int> ffKey = {-1, -1};
        int donor = -1, donorScript = -1, targetScript = -1; /* donor: index in g_game.fighters */
        int donorOf = -2;                                      /* the donor DOPT was made for */
        std::vector<std::pair<int, int>> dopt;                 /* the donor's scripts with camera commands: number, count */
        bool loaded = false, blocked = false;
        std::string error, msg, active;
        std::vector<Application> list;
        char name[64] = "", notes_[256] = "", variant[24] = "";
        int status = 0;
    } choreo;
    /* the technique whose look a script's own (9999) sprite effect takes: TechData.scriptEffect. The Fight
       command is no technique: its effect is 2 */
    int ownTech = 0;
    /* the technique that gives script SCRIPT's own effect: the chosen one when it runs that script, else
       (not for the Fight command's 5) the first that does; nullptr for none */
    auto own_tech = [&](int script) -> const TechRow * {
        for (const TechRow &t : g_techs)
            if (t.id == ownTech && t.script == script) return &t;
        if (script == 5) return nullptr;
        for (const TechRow &t : g_techs)
            if (t.script == script && t.effect) return &t;
        return nullptr;
    };
    auto own_effect_id = [&](int script) {
        const TechRow *t = own_tech(script);
        return t ? t->effect : script == 5 ? 2 : -1;
    };
    auto snap_cam = [&]() { run.cam = view.cam, run.fov = view.fov, run.camIdx = view.gameCamera, run.camSet = true; };
    std::vector<FxShot> shots;
    std::map<int, std::unique_ptr<Effect>> shotFx;
    /* the effect's own images (the mod), when it has them and they are on: they play in the original's place */
    bool shotMods = true;
    std::map<int, std::unique_ptr<CustomEffect>> shotOwnFx;
