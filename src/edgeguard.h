#include "../MexTK/mex.h"
#include "events.h"

static void Exit(int value);
static void Reset();

static void Think_Spacies(void);
static void Think_Sheik(void);
static void Think_Falcon(void);
static void Think_Marth(void);
static void ChangePlayerPercent(GOBJ *menu_gobj, int value);

// Common values -------------------------------------------------

static const char *OffOn[2] = {"Off", "On"};

typedef struct KBValues {
    float mag_min, mag_max;
    float ang_min, ang_max;
    float dmg_min, dmg_max;
} KBValues;

static const char *Values_HitStrength[] = {"Weak", "Normal", "Hard"};
static KBValues HitStrength_KBRange[] = {
    {
         80.f, 100.f, // mag
         45.f,  60.f, // ang
         10.f,  40.f, // dmg
    },
    {
        106.f, 120.f, // mag
         45.f,  60.f, // ang
         50.f,  80.f, // dmg
    },
    {
        140.f, 200.f, // mag
         65.f,  70.f, // ang
         80.f, 110.f, // dmg
    },
};

enum options_initial {
    OPT_MAIN_RECOVERY,
    OPT_MAIN_HITSTRENGTH,
    OPT_MAIN_PERCENT,
    OPT_MAIN_EXIT,
    
    OPT_MAIN_COUNT
};

static EventOption Options_Main[] = {
    {
        .kind = OPTKIND_MENU,
        .menu = 0, // Set in Event_Init
        .name = "Recovery Options",
        .desc = "Alter the CPU's recovery options.",
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Hit Strength",
        .desc = "How far the CPU will be knocked back.",
        .values = Values_HitStrength,
        .value_num = countof(Values_HitStrength),
        .val = 1,
    },
    {
        .kind = OPTKIND_INT,
        .value_num = 999,
        .name = "Player Percent",
        .desc = "Adjust the player's percent.",
        .values = "%d%%",
        .OnChange = ChangePlayerPercent,
    },
    {
        .kind = OPTKIND_FUNC,
        .name = "Exit",
        .desc = "Return to the Event Select Screen.",
        .OnSelect = Exit,
    }
};

static EventMenu Menu_Main = {
    .name = "Edgeguard",
    .option_num = sizeof(Options_Main) / sizeof(EventOption),
    .options = &Options_Main,
};

// Fox -------------------------------------------------

enum options_spacies {
    OPT_SPACIES_FF_LOW,
    OPT_SPACIES_FF_MID,
    OPT_SPACIES_FF_HIGH,
    OPT_SPACIES_JUMP,
    OPT_SPACIES_ILLUSION,
    OPT_SPACIES_FASTFALL,
};

static EventOption Options_Fox[] = {
    {
        .kind = OPTKIND_STRING,
        .name = "Firefox Low",
        .desc = "Allow Fox to aim his up special to the ledge.",
        .values = OffOn,
        .value_num = 2,
        .val = 1,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Firefox Mid",
        .desc = "Allow Fox to aim his up special to the stage.",
        .values = OffOn,
        .value_num = 2,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Firefox High",
        .desc = "Allow Fox to aim his up special high.",
        .values = OffOn,
        .value_num = 2,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Double Jump",
        .desc = "Allow Fox to double jump.",
        .values = OffOn,
        .value_num = 2,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Illusion",
        .desc = "Allow Fox to side special.",
        .values = OffOn,
        .value_num = 2,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Fast Fall",
        .desc = "Allow Fox to fast fall.",
        .values = OffOn,
        .value_num = 2,
    },
};
static EventMenu Menu_Fox = {
    .name = "Fox Recovery",
    .option_num = sizeof(Options_Fox) / sizeof(EventOption),
    .options = &Options_Fox,
};

// Falco -------------------------------------------------

static EventOption Options_Falco[] = {
    {
        .kind = OPTKIND_STRING,
        .name = "Firebird Low",
        .desc = "Allow Falco to aim his up special to the ledge.",
        .values = OffOn,
        .value_num = 2,
        .val = 1,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Firebird Mid",
        .desc = "Allow Falco to aim his up special to the stage.",
        .values = OffOn,
        .value_num = 2,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Firebird High",
        .desc = "Allow Falco to aim his up special high.",
        .values = OffOn,
        .value_num = 2,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Double Jump",
        .desc = "Allow Falco to double jump.",
        .values = OffOn,
        .value_num = 2,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Illusion",
        .desc = "Allow Falco to side special.",
        .values = OffOn,
        .value_num = 2,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Fast Fall",
        .desc = "Allow Falco to fast fall.",
        .values = OffOn,
        .value_num = 2,
    },
};
static EventMenu Menu_Falco = {
    .name = "Falco Recovery",
    .option_num = sizeof(Options_Falco) / sizeof(EventOption),
    .options = &Options_Falco,
};

// Sheik -------------------------------------------------

enum options_sheik {
    OPT_SHEIK_UPB_LEDGE,
    OPT_SHEIK_UPB_STAGE,
    OPT_SHEIK_UPB_HIGH,
    OPT_SHEIK_JUMP,
    OPT_SHEIK_FASTFALL,
    OPT_SHEIK_FAIR,
    OPT_SHEIK_AMSAH_TECH,
    
    OPT_SHEIK_COUNT
};

static EventOption Options_Sheik[] = {
    {
        .kind = OPTKIND_STRING,
        .name = "Vanish to Ledge",
        .desc = "Allow Sheik to vanish to ledge and the\ntip of the stage.",
        .values = OffOn,
        .value_num = 2,
        .val = 1,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Vanish to Stage",
        .desc = "Allow Sheik to vanish deep into stage.",
        .values = OffOn,
        .value_num = 2,
        .val = 1,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Vanish High",
        .desc = "Allow Sheik to vanish above the ledge\nand to platforms.",
        .values = OffOn,
        .value_num = 2,
        .val = 1,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Double Jump",
        .desc = "Allow Sheik to double jump.",
        .values = OffOn,
        .value_num = 2,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Fast Fall",
        .desc = "Allow Sheik to fast fall.",
        .values = OffOn,
        .value_num = 2,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Fair",
        .desc = "Allow Sheik to fair the player.",
        .values = OffOn,
        .value_num = 2,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Amsah Tech",
        .desc = "Allow Sheik to Amsah Tech.",
        .values = OffOn,
        .value_num = 2,
    },
};

static EventMenu Menu_Sheik = {
    .name = "Sheik Recovery",
    .option_num = sizeof(Options_Sheik) / sizeof(EventOption),
    .options = &Options_Sheik,
};

// Captain Falcon --------------------------------------

enum options_falcon {
    OPT_FALCON_UPB,
    OPT_FALCON_DRIFT_BACK,
    OPT_FALCON_JUMP,
    OPT_FALCON_FASTFALL,
    OPT_FALCON_DOWNB,

    OPT_FALCON_COUNT
};

static EventOption Options_Falcon[] = {
    {
        .kind = OPTKIND_STRING,
        .name = "Falcon Dive",
        .desc = "Allow Falcon to Up B.",
        .values = OffOn,
        .value_num = 2,
        .val = 1,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Drift Back",
        .desc = "Allow Falcon to drift backwards.",
        .values = OffOn,
        .value_num = 2,
        .val = 1,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Double Jump",
        .desc = "Allow Falcon to double jump.",
        .values = OffOn,
        .value_num = 2,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Fast Fall",
        .desc = "Allow Falcon to fast fall.",
        .values = OffOn,
        .value_num = 2,
    },
    {
        .kind = OPTKIND_STRING,
        .name = "Falcon Kick",
        .desc = "Allow Falcon to Down B.",
        .values = OffOn,
        .value_num = 2,
    },
};

static EventMenu Menu_Falcon = {
    .name = "Falcon Recovery",
    .option_num = sizeof(Options_Falcon) / sizeof(EventOption),
    .options = &Options_Falcon,
};

// Marth --------------------------------------

enum options_marth {
    OPT_MARTH_JUMP,
    OPT_MARTH_COUNT
};

static EventOption Options_Marth[] = {
    {
        .kind = OPTKIND_STRING,
        .name = "Double Jump",
        .desc = "Allow Marth to double jump.",
        .values = OffOn,
        .value_num = 2,
    },
};

static EventMenu Menu_Marth = {
    .name = "Marth Recovery",
    .option_num = sizeof(Options_Marth) / sizeof(EventOption),
    .options = &Options_Marth,
};

// Info lookup -----------------------------------------

typedef struct EdgeguardInfo {
    EventMenu *recovery_menu;
    void (*Think)(void);
} EdgeguardInfo;

static EdgeguardInfo InfoLookup[] = {
    {0}, // MARIO
    {
        .recovery_menu = &Menu_Fox,
        .Think = Think_Spacies,
    },
    {
        .recovery_menu = &Menu_Falcon,
        .Think = Think_Falcon,
    },
    {0}, // DK
    {0}, // KIRBY
    {0}, // BOWSER
    {0}, // LINK
    {
        .recovery_menu = &Menu_Sheik,
        .Think = Think_Sheik,
    },
    {0}, // NESS
    {0}, // PEACH
    {0}, // POPO
    {0}, // NANA
    {0}, // PIKACHU
    {0}, // SAMUS
    {0}, // YOSHI
    {0}, // JIGGLYPUFF
    {0}, // MEWTWO
    {0}, // LUIGI
    {
        .recovery_menu = &Menu_Marth,
        .Think = Think_Marth,
    },
    {0}, // ZELDA
    {0}, // YOUNGLINK
    {0}, // DRMARIO
    {
        .recovery_menu = &Menu_Falco,
        .Think = Think_Spacies,
    },
    {0}, // PICHU
    {0}, // GAW,
    {0}, // GANONDORF
    {0}, // ROY
};
