/*
 * DEVIL'S GAUNTLET
 * A devious trap-platformer built with C + Raylib
 *
 * Compile: gcc devils_gauntlet.c -o devils_gauntlet -lraylib -lm
 * Or with raylib local: gcc devils_gauntlet.c -o devils_gauntlet -I./raylib/include -L./raylib/lib -lraylib -lm
 *
 * Controls:
 *   A/D or LEFT/RIGHT  - Move
 *   SPACE/W/UP         - Jump
 *   LEFT SHIFT         - Dash
 *   R                  - Restart level
 *   ESC                - Menu / Quit
 */

#include "raylib.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ─────────────────────────── Constants ─────────────────────────── */

#define SCREEN_W        1024
#define SCREEN_H        640
#define TILE            40
#define GRAVITY         1800.0f
#define JUMP_FORCE      -620.0f
#define MOVE_SPEED      260.0f
#define DASH_FORCE      520.0f
#define DASH_DURATION   0.12f
#define DASH_COOLDOWN   1.5f
#define COYOTE_TIME     0.10f
#define MAX_TRAPS       128
#define MAX_PARTICLES   256
#define MAX_DEATH_MEM   3
#define MAX_LEVELS      10
#define COLS            (SCREEN_W / TILE)   /* 25 */
#define ROWS            (SCREEN_H / TILE)   /* 16 */

/* ─────────────────────────── Enums ─────────────────────────── */

typedef enum {
    SCREEN_MENU,
    SCREEN_PLAY,
    SCREEN_DEAD,
    SCREEN_WIN_LEVEL,
    SCREEN_WIN_GAME
} GameScreen;

typedef enum {
    TRAP_SPIKE_UP,
    TRAP_SPIKE_DOWN,
    TRAP_CRUSHER_V,   /* vertical crusher */
    TRAP_CRUSHER_H,   /* horizontal crusher */
    TRAP_SAW,
    TRAP_FIRE,
    TRAP_FAKE_PLATFORM,
    TRAP_MAGNET,
    TRAP_VANISH_FLOOR,
    TRAP_DECOY_DOOR,
} TrapType;

/* ─────────────────────────── Structs ─────────────────────────── */

typedef struct {
    Vector2 pos;
    Vector2 vel;
    Vector2 size;
    bool    on_ground;
    bool    was_on_ground;
    float   coyote_timer;
    bool    dash_available;
    float   dash_timer;       /* > 0 while dashing */
    float   dash_cooldown;
    Vector2 dash_dir;         /* 2D direction vector */
    bool    can_double_jump;
    float   land_squash;
    bool    alive;
    int     deaths;
    Vector2 death_mem[MAX_DEATH_MEM];
    int     death_mem_count;
    Vector2 spawn;
} Player;

typedef struct {
    TrapType type;
    Rectangle rect;         /* world rect */
    bool active;
    bool triggered;         /* one-shot trigger */
    bool revealed;          /* fake platforms: revealed after first death */
    float timer;
    float phase;            /* phase offset for animations */
    float move_t;           /* 0..1 lerp for crushers */
    int   move_dir;         /* +1 expanding, -1 contracting */
    Rectangle origin;       /* crusher start rect */
    Rectangle target;       /* crusher end rect */
    bool is_adaptive;       /* spawned by death memory system */
} Trap;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    Color   color;
    float   life;
    float   max_life;
    float   size;
} Particle;

typedef struct {
    /* Tile map: 0=empty, 1=solid, 2=door(real), 3=door(decoy), 9=start */
    int     tiles[ROWS][COLS];
    Trap    traps[MAX_TRAPS];
    int     trap_count;
    /* Special flags */
    bool    gravity_flip_enabled;
    float   gravity_flip_interval; /* seconds between flips */
    bool    mirror_mode;
    float   mirror_interval;
    bool    time_warp;
    float   time_scale;            /* current scale */
    float   mirror_timer;
    float   gravity_timer;
    bool    gravity_flipped;
    int     level_index;
    char    name[64];
    char    hint[128];
} Level;

typedef struct {
    Player      player;
    Level       level;
    Particle    particles[MAX_PARTICLES];
    int         particle_count;
    GameScreen  screen;
    int         current_level;
    int         total_deaths;
    float       dead_timer;       /* delay before respawn screen */
    float       win_timer;
    float       flash_alpha;      /* white flash on death */
    bool        gravity_dir;      /* false = down, true = up */
    float       camera_shake;
    float       level_title_timer;
    /* UI state */
    int         selected_menu;
} Game;

/* ─────────────────────────── Color palette ─────────────────────────── */

#define COL_BG          (Color){15, 12, 20, 255}
#define COL_TILE        (Color){45, 40, 60, 255}
#define COL_TILE_EDGE   (Color){70, 60, 90, 255}
#define COL_PLAYER      (Color){255, 220, 80, 255}
#define COL_PLAYER2     (Color){220, 160, 40, 255}
#define COL_SPIKE       (Color){200, 60, 60, 255}
#define COL_SPIKE2      (Color){255, 100, 80, 255}
#define COL_CRUSHER     (Color){80, 80, 140, 255}
#define COL_CRUSHER2    (Color){120, 120, 200, 255}
#define COL_SAW         (Color){220, 180, 50, 255}
#define COL_FIRE        (Color){255, 120, 20, 255}
#define COL_FIRE2       (Color){255, 60, 0, 255}
#define COL_FAKE        (Color){60, 100, 60, 255}
#define COL_DOOR        (Color){80, 200, 120, 255}
#define COL_DOOR2       (Color){40, 140, 70, 255}
#define COL_DECOY       (Color){200, 80, 80, 255}
#define COL_MAGNET      (Color){100, 140, 220, 255}
#define COL_VANISH      (Color){140, 80, 160, 255}
#define COL_ADAPTIVE    (Color){255, 80, 160, 255}
#define COL_TEXT        (Color){230, 225, 240, 255}
#define COL_TEXT2       (Color){160, 150, 180, 255}
#define COL_WARN        (Color){255, 180, 50, 255}
#define COL_GRAV        (Color){100, 220, 255, 255}

/* ─────────────────────────── Level definitions ─────────────────────────── */

/*
 * Tile codes:
 *  0 = empty
 *  1 = solid wall/floor
 *  2 = real door (exit)
 *  3 = decoy door (trap)
 *  9 = player spawn
 */

/* Helper macros so level arrays are readable */
#define _ 0
#define W 1
#define D 2
#define X 3
#define S 9

/* ═══════════════════ LEVEL 1: Tutorial Lie ═══════════════════ */
static int L1_TILES[ROWS][COLS] = {
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,S,_,_,_,_,_,W,W,W,W,_,_,_,W,W,W,W,W,_,_,X,_,D,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
};

/* ═══════════════════ LEVEL 2: Spike Garden ═══════════════════ */
static int L2_TILES[ROWS][COLS] = {
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,W,W,_,_,_,W,W,_,_,_,W,W,_,_,_,W,W,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,S,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,D,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
};

/* ═══════════════════ LEVEL 3: Phantom Floor ═══════════════════ */
static int L3_TILES[ROWS][COLS] = {
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,W,W,W,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,W,W,W,_,_,_,_,_,_,_,_,_,W,W,W,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,S,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,D,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
};

/* ═══════════════════ LEVEL 4: Crusher Gauntlet ═══════════════════ */
static int L4_TILES[ROWS][COLS] = {
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,S,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,D,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
};

/* ═══════════════════ LEVEL 5: Gravity Flip Zone ═══════════════════ */
static int L5_TILES[ROWS][COLS] = {
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,W,W,W,_,_,_,_,_,_,W,W,W,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,W,W,W,_,_,_,_,_,_,_,W,W,W,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,S,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,D,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
};

/* ═══════════════════ LEVEL 6: Time Warp Maze ═══════════════════ */
static int L6_TILES[ROWS][COLS] = {
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,W,_,_,_,_,_,W,_,_,_,_,_,W,_,_,_,_,_,W,_,W},
    {W,_,_,_,W,_,_,_,_,_,W,_,_,_,_,_,W,_,_,_,_,_,W,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,S,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,D,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
};

/* ═══════════════════ LEVEL 7: Wrong Door ═══════════════════ */
static int L7_TILES[ROWS][COLS] = {
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,S,_,_,_,_,_,X,_,_,_,X,_,_,_,X,_,_,_,X,_,_,_,D,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
};

/* ═══════════════════ LEVEL 8: Mirror Madness ═══════════════════ */
static int L8_TILES[ROWS][COLS] = {
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,S,_,_,_,_,W,W,W,_,_,_,W,W,W,_,_,_,W,W,W,_,_,D,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
};

/* ═══════════════════ LEVEL 9: Memory Hell ═══════════════════ */
static int L9_TILES[ROWS][COLS] = {
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,W,W,W,_,_,_,_,_,_,W,W,W,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W,W,W,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,W,W,W,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,S,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,D,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
};

/* ═══════════════════ LEVEL 10: Devil's Deal ═══════════════════ */
static int L10_TILES[ROWS][COLS] = {
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,W,W,W,W,_,_,_,_,_,W,W,W,W,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,W,W,W,W,_,_,_,_,_,W,W,W,W,_,_,_,_,_,W,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,W},
    {W,S,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,D,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
    {W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W,W},
};

/* ─────────────────────────── Forward declarations ─────────────────────────── */
static void  add_particle(Game *g, Vector2 pos, Color col, float speed, int count);
static void  kill_player(Game *g);
static void  load_level(Game *g, int idx);
static void  add_trap(Level *lv, Trap t);

/* ─────────────────────────── Level loader ─────────────────────────── */

static void add_trap(Level *lv, Trap t) {
    if (lv->trap_count >= MAX_TRAPS) return;
    lv->traps[lv->trap_count++] = t;
}

static Trap make_spike(float x, float y, float w, float h, TrapType type, float phase) {
    Trap t = {0};
    t.type   = type;
    t.rect   = (Rectangle){x, y, w, h};
    t.active = true;
    t.phase  = phase;
    return t;
}

static Trap make_crusher_v(float x, float y_top, float height, float travel, float phase) {
    Trap t = {0};
    t.type    = TRAP_CRUSHER_V;
    t.origin  = (Rectangle){x, y_top, (float)TILE, height};
    t.target  = (Rectangle){x, y_top + travel, (float)TILE, height};
    t.rect    = t.origin;
    t.active  = true;
    t.phase   = phase;
    t.move_dir = 1;
    return t;
}

static Trap make_crusher_h(float x_left, float y, float width, float travel, float phase) {
    Trap t = {0};
    t.type    = TRAP_CRUSHER_H;
    t.origin  = (Rectangle){x_left, y, width, (float)TILE};
    t.target  = (Rectangle){x_left + travel, y, width, (float)TILE};
    t.rect    = t.origin;
    t.active  = true;
    t.phase   = phase;
    t.move_dir = 1;
    return t;
}

static Trap make_fire(float x, float y, float phase) {
    Trap t = {0};
    t.type   = TRAP_FIRE;
    t.rect   = (Rectangle){x, y - (float)TILE, (float)TILE, (float)TILE};
    t.active = false; /* toggled by timer */
    t.phase  = phase;
    return t;
}

static Trap make_fake(float x, float y, float w, float h) {
    Trap t = {0};
    t.type     = TRAP_FAKE_PLATFORM;
    t.rect     = (Rectangle){x, y, w, h};
    t.active   = true;
    t.revealed = false;
    return t;
}

static Trap make_vanish(float x, float y, float w) {
    Trap t = {0};
    t.type   = TRAP_VANISH_FLOOR;
    t.rect   = (Rectangle){x, y, w, (float)TILE/2};
    t.active = true;
    t.timer  = 0.0f;
    return t;
}

static Trap make_saw(float x, float y, float phase) {
    Trap t = {0};
    t.type   = TRAP_SAW;
    t.rect   = (Rectangle){x, y, (float)TILE, (float)TILE};
    t.active = true;
    t.phase  = phase;
    return t;
}

static Trap make_magnet(float x, float y) {
    Trap t = {0};
    t.type   = TRAP_MAGNET;
    t.rect   = (Rectangle){x - TILE*2, y, TILE*4, TILE*3};
    t.active = true;
    return t;
}

static void load_level(Game *g, int idx) {
    Level *lv = &g->level;
    memset(lv, 0, sizeof(Level));
    lv->level_index  = idx;
    lv->time_scale   = 1.0f;
    g->gravity_dir   = false;
    g->level_title_timer = 2.0f;

    /* ── Copy tile map ── */
    int (*src)[COLS] = NULL;
    switch (idx) {
        case 0: src = L1_TILES;  break;
        case 1: src = L2_TILES;  break;
        case 2: src = L3_TILES;  break;
        case 3: src = L4_TILES;  break;
        case 4: src = L5_TILES;  break;
        case 5: src = L6_TILES;  break;
        case 6: src = L7_TILES;  break;
        case 7: src = L8_TILES;  break;
        case 8: src = L9_TILES;  break;
        case 9: src = L10_TILES; break;
        default: src = L1_TILES; break;
    }
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            lv->tiles[r][c] = src[r][c];

    /* ── Find spawn ── */
    Vector2 spawn = {2*TILE, 12*TILE};
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (lv->tiles[r][c] == S) {
                spawn = (Vector2){c*TILE + 4, r*TILE};
                lv->tiles[r][c] = 0; /* clear spawn tile */
            }

    /* ── Init player ── */
    Player *p = &g->player;
    p->pos             = spawn;
    p->spawn           = spawn;
    p->vel             = (Vector2){0, 0};
    p->size            = (Vector2){24, 32};
    p->alive           = true;
    p->on_ground       = false;
    p->was_on_ground   = false;
    p->dash_available  = true;
    p->dash_timer      = 0.0f;
    p->dash_cooldown   = 0.0f;
    p->dash_dir        = (Vector2){0, 0};
    p->can_double_jump = true;
    p->land_squash     = 0.0f;
    p->coyote_timer    = 0.0f;
    p->death_mem_count = 0; /* Clear death memory when starting a level */

    /* ── Build trap set per level ── */
    switch (idx) {
        case 0: /* Tutorial Lie */
            strcpy(lv->name, "Level 1: Tutorial Lie");
            strcpy(lv->hint, "Some doors are a lie. Find the real path.");
            /* spike just before door */
            add_trap(lv, make_spike(21*TILE, 12*TILE, TILE, TILE/2, TRAP_SPIKE_UP, 0));
            /* gap between platforms has floor spikes */
            add_trap(lv, make_spike(11*TILE, 12*TILE, 3*TILE, TILE/2, TRAP_SPIKE_UP, 0));
            break;

        case 1: /* Spike Garden */
            strcpy(lv->name, "Level 2: Spike Garden");
            strcpy(lv->hint, "Time your jumps onto the platforms.");
            /* floor spikes in gaps */
            add_trap(lv, make_spike(2*TILE,  12*TILE, 3*TILE, TILE/2, TRAP_SPIKE_UP,   0.0f));
            add_trap(lv, make_spike(7*TILE,  12*TILE, 3*TILE, TILE/2, TRAP_SPIKE_UP,   0.3f));
            add_trap(lv, make_spike(12*TILE, 12*TILE, 3*TILE, TILE/2, TRAP_SPIKE_UP,   0.6f));
            add_trap(lv, make_spike(17*TILE, 12*TILE, 3*TILE, TILE/2, TRAP_SPIKE_UP,   0.0f));
            add_trap(lv, make_spike(22*TILE, 12*TILE, TILE,   TILE/2, TRAP_SPIKE_UP,   0.3f));
            /* ceiling spikes */
            add_trap(lv, make_spike(4*TILE,  1*TILE,  5*TILE, TILE/2, TRAP_SPIKE_DOWN, 0.0f));
            add_trap(lv, make_spike(13*TILE, 1*TILE,  6*TILE, TILE/2, TRAP_SPIKE_DOWN, 0.5f));
            /* saws on platforms (placed at row 9, which sits on top of row 10 platforms) */
            add_trap(lv, make_saw(6*TILE,  9*TILE, 0.0f));
            add_trap(lv, make_saw(16*TILE, 9*TILE, 0.5f));
            break;

        case 2: /* Phantom Floor */
            strcpy(lv->name, "Level 3: Phantom Floor");
            strcpy(lv->hint, "Ghostly red platforms are fake! Jump over them.");
            /* real floor spikes covering the pit */
            add_trap(lv, make_spike(2*TILE, 12*TILE, 21*TILE, TILE/2, TRAP_SPIKE_UP, 0));
            /* fake platforms bridging the gaps */
            add_trap(lv, make_fake(7*TILE,  9*TILE,  3*TILE, TILE/2));
            add_trap(lv, make_fake(13*TILE, 6*TILE,  3*TILE, TILE/2));
            add_trap(lv, make_fake(19*TILE, 9*TILE,  3*TILE, TILE/2));
            break;

        case 3: /* Crusher Gauntlet */
            strcpy(lv->name, "Level 4: Crusher Gauntlet");
            strcpy(lv->hint, "Watch the red laser warning guides.");
            /* vertical crushers dropping from ceiling */
            add_trap(lv, make_crusher_v(4*TILE,  1*TILE, TILE*2, TILE*9, 0.0f));
            add_trap(lv, make_crusher_v(7*TILE,  1*TILE, TILE*2, TILE*9, 0.5f));
            add_trap(lv, make_crusher_v(10*TILE, 1*TILE, TILE*2, TILE*9, 0.25f));
            add_trap(lv, make_crusher_v(13*TILE, 1*TILE, TILE*2, TILE*9, 0.75f));
            add_trap(lv, make_crusher_v(16*TILE, 1*TILE, TILE*2, TILE*9, 0.1f));
            add_trap(lv, make_crusher_v(19*TILE, 1*TILE, TILE*2, TILE*9, 0.6f));
            /* floor spikes between crushers */
            add_trap(lv, make_spike(3*TILE,  12*TILE, TILE, TILE/2, TRAP_SPIKE_UP, 0));
            add_trap(lv, make_spike(6*TILE,  12*TILE, TILE, TILE/2, TRAP_SPIKE_UP, 0));
            add_trap(lv, make_spike(9*TILE,  12*TILE, TILE, TILE/2, TRAP_SPIKE_UP, 0));
            add_trap(lv, make_spike(12*TILE, 12*TILE, TILE, TILE/2, TRAP_SPIKE_UP, 0));
            add_trap(lv, make_spike(15*TILE, 12*TILE, TILE, TILE/2, TRAP_SPIKE_UP, 0));
            add_trap(lv, make_spike(18*TILE, 12*TILE, TILE, TILE/2, TRAP_SPIKE_UP, 0));
            break;

        case 4: /* Gravity Flip Zone */
            strcpy(lv->name, "Level 5: Gravity Flip Zone");
            strcpy(lv->hint, "Safe platforms protect you from ceiling spikes.");
            lv->gravity_flip_enabled  = true;
            lv->gravity_flip_interval = 3.0f;
            /* spikes on both ceiling and floor */
            add_trap(lv, make_spike(2*TILE,  12*TILE, 2*TILE, TILE/2, TRAP_SPIKE_UP,   0.0f));
            add_trap(lv, make_spike(7*TILE,  12*TILE, 7*TILE, TILE/2, TRAP_SPIKE_UP,   0.0f));
            add_trap(lv, make_spike(17*TILE, 12*TILE, 6*TILE, TILE/2, TRAP_SPIKE_UP,   0.0f));
            
            add_trap(lv, make_spike(4*TILE,  1*TILE,  5*TILE, TILE/2, TRAP_SPIKE_DOWN, 0.0f));
            add_trap(lv, make_spike(12*TILE, 1*TILE,  6*TILE, TILE/2, TRAP_SPIKE_DOWN, 0.0f));
            break;

        case 5: /* Time Warp Maze */
            strcpy(lv->name, "Level 6: Time Warp Maze");
            strcpy(lv->hint, "Time bends around you. Adjust to the speeds.");
            lv->time_warp = true;
            /* fire jets in corridor gaps */
            add_trap(lv, make_fire(6*TILE,  13*TILE, 0.0f));
            add_trap(lv, make_fire(11*TILE, 13*TILE, 0.25f));
            add_trap(lv, make_fire(17*TILE, 13*TILE, 0.0f));
            add_trap(lv, make_fire(22*TILE, 13*TILE, 0.5f));
            /* ceiling spikes in corridors */
            add_trap(lv, make_spike(7*TILE,  1*TILE, 2*TILE, TILE/2, TRAP_SPIKE_DOWN, 0.0f));
            add_trap(lv, make_spike(12*TILE, 1*TILE, 3*TILE, TILE/2, TRAP_SPIKE_DOWN, 0.3f));
            /* saws on pillars (pillars are at cols 4, 10, 16, 22; row 3) */
            add_trap(lv, make_saw(4*TILE,  2*TILE, 0.0f));
            add_trap(lv, make_saw(10*TILE, 2*TILE, 0.4f));
            add_trap(lv, make_saw(16*TILE, 2*TILE, 0.2f));
            add_trap(lv, make_saw(22*TILE, 2*TILE, 0.6f));
            break;

        case 6: /* Wrong Door */
            strcpy(lv->name, "Level 7: Wrong Door");
            strcpy(lv->hint, "Choose wisely. Touch decoy doors at your own risk!");
            /* floor spikes that trigger on the level */
            add_trap(lv, make_spike(2*TILE,  12*TILE, 5*TILE, TILE/2, TRAP_SPIKE_UP, 0.0f));
            add_trap(lv, make_spike(8*TILE,  12*TILE, 3*TILE, TILE/2, TRAP_SPIKE_UP, 0.0f));
            add_trap(lv, make_spike(12*TILE, 12*TILE, 3*TILE, TILE/2, TRAP_SPIKE_UP, 0.0f));
            add_trap(lv, make_spike(16*TILE, 12*TILE, 3*TILE, TILE/2, TRAP_SPIKE_UP, 0.0f));
            /* crusher for drama */
            add_trap(lv, make_crusher_v(5*TILE, 1*TILE, TILE*2, TILE*9, 0.0f));
            add_trap(lv, make_crusher_v(13*TILE, 1*TILE, TILE*2, TILE*9, 0.3f));
            add_trap(lv, make_crusher_v(18*TILE, 1*TILE, TILE*2, TILE*9, 0.6f));
            break;

        case 7: /* Mirror Madness */
            strcpy(lv->name, "Level 8: Mirror Madness");
            strcpy(lv->hint, "Left is right. Right is left. Timing is key.");
            lv->mirror_mode     = true;
            lv->mirror_interval = 4.0f;
            /* saws on top of the blocks (blocks are row 12, cols 6-8, 12-14, 18-20) */
            add_trap(lv, make_saw(7*TILE,  11*TILE, 0.0f));
            add_trap(lv, make_saw(13*TILE, 11*TILE, 0.33f));
            add_trap(lv, make_saw(19*TILE, 11*TILE, 0.66f));
            /* crushers stopping just above the blocks */
            add_trap(lv, make_crusher_v(6*TILE,  1*TILE, TILE*2, TILE*8, 0.2f));
            add_trap(lv, make_crusher_v(12*TILE, 1*TILE, TILE*2, TILE*8, 0.5f));
            add_trap(lv, make_crusher_v(18*TILE, 1*TILE, TILE*2, TILE*8, 0.8f));
            /* spikes in the gaps between blocks */
            add_trap(lv, make_spike(9*TILE,  12*TILE, 3*TILE, TILE/2, TRAP_SPIKE_UP, 0.0f));
            add_trap(lv, make_spike(15*TILE, 12*TILE, 3*TILE, TILE/2, TRAP_SPIKE_UP, 0.0f));
            break;

        case 8: /* Memory Hell */
            strcpy(lv->name, "Level 9: Memory Hell");
            strcpy(lv->hint, "Everything at once! Rely on your adaptive spikes.");
            lv->gravity_flip_enabled  = true;
            lv->gravity_flip_interval = 4.0f;
            lv->mirror_mode           = true;
            lv->mirror_interval       = 5.0f;
            /* crushers */
            add_trap(lv, make_crusher_v(5*TILE,  1*TILE, TILE*2, TILE*9, 0.0f));
            add_trap(lv, make_crusher_v(15*TILE, 1*TILE, TILE*2, TILE*9, 0.7f));
            /* spikes */
            add_trap(lv, make_spike(2*TILE,  12*TILE, 3*TILE, TILE/2, TRAP_SPIKE_UP,   0.0f));
            add_trap(lv, make_spike(13*TILE, 12*TILE, 2*TILE, TILE/2, TRAP_SPIKE_UP,   0.0f));
            add_trap(lv, make_spike(18*TILE, 12*TILE, 3*TILE, TILE/2, TRAP_SPIKE_UP,   0.0f));
            add_trap(lv, make_spike(3*TILE,  1*TILE,  3*TILE, TILE/2, TRAP_SPIKE_DOWN, 0.0f));
            add_trap(lv, make_spike(11*TILE, 1*TILE,  2*TILE, TILE/2, TRAP_SPIKE_DOWN, 0.0f));
            add_trap(lv, make_spike(17*TILE, 1*TILE,  3*TILE, TILE/2, TRAP_SPIKE_DOWN, 0.0f));
            /* saws correctly placed on platforms:
               - row 4 platforms: cols 3-5, cols 12-14
               - row 6 platform: cols 18-20
               - row 9 platform: cols 9-11 */
            add_trap(lv, make_saw(4*TILE,  3*TILE, 0.0f));
            add_trap(lv, make_saw(13*TILE, 3*TILE, 0.5f));
            add_trap(lv, make_saw(19*TILE, 5*TILE, 0.25f));
            add_trap(lv, make_saw(10*TILE, 8*TILE, 0.75f));
            /* fire jets */
            add_trap(lv, make_fire(8*TILE,  13*TILE, 0.0f));
            add_trap(lv, make_fire(16*TILE, 13*TILE, 0.5f));
            break;

        case 9: /* Devil's Deal */
            strcpy(lv->name, "Level 10: The Devil's Deal");
            strcpy(lv->hint, "Stay in the air! Dash and use gravity flips to survive.");
            lv->gravity_flip_enabled  = true;
            lv->gravity_flip_interval = 2.2f;
            lv->mirror_mode           = true;
            lv->mirror_interval       = 3.2f;
            /* wall crushers sweeping horizontally */
            add_trap(lv, make_crusher_h(1*TILE,  3*TILE,  TILE*3, TILE*5, 0.0f));
            add_trap(lv, make_crusher_h(1*TILE,  7*TILE,  TILE*3, TILE*5, 0.5f));
            add_trap(lv, make_crusher_h(21*TILE, 3*TILE,  TILE*3, TILE*5, 0.25f));
            add_trap(lv, make_crusher_h(21*TILE, 7*TILE,  TILE*3, TILE*5, 0.75f));
            /* ceiling spikes covering the whole ceiling */
            add_trap(lv, make_spike(2*TILE,  1*TILE,  20*TILE, TILE/2, TRAP_SPIKE_DOWN, 0.0f));
            /* floor spikes covering the whole floor */
            add_trap(lv, make_spike(2*TILE,  12*TILE, 20*TILE, TILE/2, TRAP_SPIKE_UP,   0.0f));
            /* saws on platforms */
            add_trap(lv, make_saw(10*TILE, 7*TILE, 0.0f));
            add_trap(lv, make_saw(18*TILE, 7*TILE, 0.5f));
            /* fire jets */
            add_trap(lv, make_fire(5*TILE,  9*TILE, 0.0f));
            add_trap(lv, make_fire(13*TILE, 9*TILE, 0.5f));
            break;
    }
}

/* ─────────────────────────── Particles ─────────────────────────── */

static void add_particle(Game *g, Vector2 pos, Color col, float speed, int count) {
    for (int i = 0; i < count && g->particle_count < MAX_PARTICLES; i++) {
        Particle *p = &g->particles[g->particle_count++];
        float angle = ((float)(GetRandomValue(0, 360))) * DEG2RAD;
        float spd   = speed * (0.5f + (float)GetRandomValue(0,100)/100.0f);
        p->pos      = pos;
        p->vel      = (Vector2){cosf(angle)*spd, sinf(angle)*spd};
        p->color    = col;
        p->max_life = 0.3f + (float)GetRandomValue(0,40)/100.0f;
        p->life     = p->max_life;
        p->size     = 3.0f + (float)GetRandomValue(0,5);
    }
}

static void update_particles(Game *g, float dt) {
    for (int i = 0; i < g->particle_count; ) {
        Particle *p = &g->particles[i];
        p->life  -= dt;
        p->pos.x += p->vel.x * dt;
        p->pos.y += p->vel.y * dt;
        p->vel.y += 200.0f * dt; /* gravity on particles */
        if (p->life <= 0) {
            g->particles[i] = g->particles[--g->particle_count];
        } else {
            i++;
        }
    }
}

static void draw_particles(Game *g) {
    for (int i = 0; i < g->particle_count; i++) {
        Particle *p = &g->particles[i];
        float alpha = p->life / p->max_life;
        Color c = p->color;
        c.a = (unsigned char)(alpha * 255);
        DrawRectangle((int)p->pos.x, (int)p->pos.y, (int)p->size, (int)p->size, c);
    }
}

/* ─────────────────────────── Collision helpers ─────────────────────────── */

static Rectangle player_rect(Player *p) {
    return (Rectangle){p->pos.x, p->pos.y, p->size.x, p->size.y};
}

static bool tile_solid(Level *lv, int r, int c) {
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return true;
    int v = lv->tiles[r][c];
    return v == 1; /* solid wall */
}

static bool check_tile_collision(Level *lv, Player *p, float *push_x, float *push_y) {
    Rectangle pr = player_rect(p);
    *push_x = 0; *push_y = 0;
    bool hit = false;

    int r0 = (int)(pr.y / TILE) - 1;
    int r1 = (int)((pr.y + pr.height) / TILE) + 1;
    int c0 = (int)(pr.x / TILE) - 1;
    int c1 = (int)((pr.x + pr.width) / TILE) + 1;

    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            if (!tile_solid(lv, r, c)) continue;
            Rectangle tr = {(float)(c*TILE), (float)(r*TILE), (float)TILE, (float)TILE};
            Rectangle ov = GetCollisionRec(pr, tr);
            if (ov.width <= 0 || ov.height <= 0) continue;
            hit = true;
            /* push out on smallest axis */
            if (ov.width < ov.height) {
                if (pr.x < tr.x) *push_x -= ov.width;
                else              *push_x += ov.width;
            } else {
                if (pr.y < tr.y) *push_y -= ov.height;
                else              *push_y += ov.height;
            }
        }
    }
    return hit;
}

/* ─────────────────────────── Death / Respawn ─────────────────────────── */

static void kill_player(Game *g) {
    Player *p = &g->player;
    if (!p->alive) return;
    p->alive = false;
    p->deaths++;
    g->total_deaths++;

    /* store death position in memory ring */
    if (p->death_mem_count < MAX_DEATH_MEM)
        p->death_mem[p->death_mem_count++] = p->pos;
    else {
        /* shift and add */
        p->death_mem[0] = p->death_mem[1];
        p->death_mem[1] = p->death_mem[2];
        p->death_mem[2] = p->pos;
    }

    /* Reveal fake platforms on death */
    for (int i = 0; i < g->level.trap_count; i++) {
        if (g->level.traps[i].type == TRAP_FAKE_PLATFORM)
            g->level.traps[i].revealed = true;
    }

    /* Particle burst */
    add_particle(g, (Vector2){p->pos.x + p->size.x/2, p->pos.y + p->size.y/2},
                 COL_PLAYER, 300.0f, 30);
    add_particle(g, (Vector2){p->pos.x + p->size.x/2, p->pos.y + p->size.y/2},
                 COL_SPIKE, 200.0f, 15);

    g->flash_alpha   = 1.0f;
    g->camera_shake  = 0.25f;
    g->dead_timer    = 0.6f; /* slightly faster restart delay */
    g->screen        = SCREEN_DEAD;
}

static void respawn(Game *g) {
    Player *p = &g->player;
    p->pos            = p->spawn;
    p->vel            = (Vector2){0, 0};
    p->alive          = true;
    p->on_ground      = false;
    p->was_on_ground  = false;
    p->dash_available = true;
    p->dash_timer     = 0.0f;
    p->dash_cooldown  = 0.0f;
    p->dash_dir        = (Vector2){0, 0};
    p->can_double_jump = true;
    p->land_squash     = 0.0f;
    p->coyote_timer   = 0.0f;
    g->gravity_dir    = false;
    g->screen         = SCREEN_PLAY;

    /* Reset time warp */
    g->level.time_scale = 1.0f;

    /* Clean up existing adaptive traps to prevent duplication on successive respawns */
    int write_idx = 0;
    for (int i = 0; i < g->level.trap_count; i++) {
        if (!g->level.traps[i].is_adaptive) {
            g->level.traps[write_idx++] = g->level.traps[i];
        }
    }
    g->level.trap_count = write_idx;

    /* Freshly populate adaptive traps from death memory */
    for (int d = 0; d < p->death_mem_count; d++) {
        Vector2 dp = p->death_mem[d];
        /* only if level has space and not on the floor*/
        if (dp.y < (ROWS-3)*TILE && g->level.trap_count < MAX_TRAPS - 2) {
            Trap t = make_spike(dp.x - TILE/2, (float)((int)(dp.y/TILE)+1)*TILE,
                                TILE, TILE/2, TRAP_SPIKE_UP, (float)d * 0.3f);
            t.is_adaptive = true;
            add_trap(&g->level, t);
        }
    }
}

/* ─────────────────────────── Update ─────────────────────────── */

static void update_game(Game *g, float dt) {
    Level  *lv = &g->level;
    Player *p  = &g->player;

    /* Time warp zones: cycle time_scale */
    if (lv->time_warp) {
        float t = (float)GetTime() * 0.5f;
        lv->time_scale = 0.5f + 0.5f * sinf(t * 3.14159f);
        lv->time_scale = 0.4f + lv->time_scale * 1.2f; /* range 0.4..1.6 */
    }
    dt *= lv->time_scale;

    /* Level title banner timer */
    if (g->level_title_timer > 0) {
        g->level_title_timer -= dt;
        if (g->level_title_timer < 0) g->level_title_timer = 0;
    }

    /* Gravity flip timer */
    if (lv->gravity_flip_enabled) {
        lv->gravity_timer += dt;
        if (lv->gravity_timer >= lv->gravity_flip_interval) {
            lv->gravity_timer = 0;
            g->gravity_dir = !g->gravity_dir;
            p->vel.y = g->gravity_dir ? -200.0f : 200.0f;
            add_particle(g, (Vector2){SCREEN_W/2, SCREEN_H/2}, COL_GRAV, 150.0f, 20);
        }
    }

    /* Mirror flip timer */
    if (lv->mirror_mode) {
        lv->mirror_timer += dt;
        if (lv->mirror_timer >= lv->mirror_interval) {
            lv->mirror_timer = 0;
            /* Flip player X around center */
            p->pos.x = SCREEN_W - p->pos.x - p->size.x;
            p->vel.x = -p->vel.x;
        }
    }

    /* Camera shake */
    if (g->camera_shake > 0) g->camera_shake -= dt * 3.0f;
    if (g->camera_shake < 0) g->camera_shake = 0;

    /* Flash fade */
    if (g->flash_alpha > 0) g->flash_alpha -= dt * 4.0f;

    /* Track ground status for landing particles and squash */
    bool was_on_ground_prev = p->on_ground;

    /* ── Input ── */
    float grav = g->gravity_dir ? -GRAVITY : GRAVITY;
    float move = 0;
    if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) move -= 1.0f;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) move += 1.0f;

    /* Mirror mode: flip input */
    if (lv->mirror_mode) move = -move;

    bool jump_pressed = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP);
    bool dash_pressed = IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT);

    /* Dash */
    p->dash_cooldown -= dt;
    if (p->dash_timer > 0) {
        p->dash_timer -= dt;
        p->vel.x = p->dash_dir.x * DASH_FORCE;
        p->vel.y = p->dash_dir.y * DASH_FORCE;
    } else if (dash_pressed && p->dash_cooldown <= 0 && p->dash_available) {
        Vector2 dir = {0, 0};
        if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A)) dir.x -= 1.0f;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) dir.x += 1.0f;
        if (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W)) dir.y -= 1.0f;
        if (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S)) dir.y += 1.0f;

        /* Normalize direction */
        if (dir.x != 0 || dir.y != 0) {
            float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
            dir.x /= len;
            dir.y /= len;
        } else {
            /* Default to facing direction */
            dir.x = (move != 0) ? move : (p->vel.x >= 0 ? 1.0f : -1.0f);
            dir.y = 0;
        }

        p->dash_dir      = dir;
        p->dash_timer    = DASH_DURATION;
        p->dash_cooldown = DASH_COOLDOWN;
        p->dash_available = false;
        add_particle(g, (Vector2){p->pos.x + p->size.x/2, p->pos.y + p->size.y/2},
                     COL_PLAYER2, 120.0f, 8);
    } else {
        /* Normal horizontal movement */
        p->vel.x = move * MOVE_SPEED;
    }

    /* Coyote time */
    if (p->on_ground) {
        p->coyote_timer   = COYOTE_TIME;
        p->dash_available = true;
        p->can_double_jump = true;
    } else {
        p->coyote_timer -= dt;
    }

    /* Jump and Double Jump */
    if (jump_pressed) {
        if (p->coyote_timer > 0) {
            float jf = g->gravity_dir ? -JUMP_FORCE : JUMP_FORCE;
            p->vel.y      = jf;
            p->coyote_timer = 0;
            /* Jump dust particles */
            add_particle(g, (Vector2){p->pos.x + p->size.x/2, g->gravity_dir ? p->pos.y : p->pos.y + p->size.y}, COL_TILE_EDGE, 100.0f, 8);
        } else if (p->can_double_jump) {
            float jf = g->gravity_dir ? -JUMP_FORCE : JUMP_FORCE;
            p->vel.y      = jf;
            p->can_double_jump = false;
            /* Double jump blast particles */
            add_particle(g, (Vector2){p->pos.x + p->size.x/2, p->pos.y + p->size.y/2}, COL_GRAV, 150.0f, 12);
        }
    }

    /* Apply gravity only when not dashing */
    if (p->dash_timer <= 0) {
        p->vel.y += grav * dt;
    }

    /* Clamp fall speed */
    if (!g->gravity_dir && p->vel.y >  800.0f) p->vel.y =  800.0f;
    if ( g->gravity_dir && p->vel.y < -800.0f) p->vel.y = -800.0f;

    /* Move X */
    p->pos.x += p->vel.x * dt;
    float px, py;
    check_tile_collision(lv, p, &px, &py);
    p->pos.x += px;
    if (px != 0) p->vel.x = 0;

    /* Move Y */
    p->on_ground = false;
    p->pos.y += p->vel.y * dt;
    check_tile_collision(lv, p, &px, &py);
    p->pos.y += py;
    if (py != 0) {
        if (!g->gravity_dir && py < 0) p->on_ground = true;
        if ( g->gravity_dir && py > 0) p->on_ground = true;
        p->vel.y = 0;
    }

    /* Landing check */
    if (p->on_ground && !was_on_ground_prev) {
        p->land_squash = 0.18f;
        add_particle(g, (Vector2){p->pos.x + p->size.x/2, g->gravity_dir ? p->pos.y : p->pos.y + p->size.y}, COL_TILE_EDGE, 80.0f, 6);
    }

    /* Land squash decay */
    if (p->land_squash > 0) {
        p->land_squash -= dt * 4.0f;
        if (p->land_squash < 0) p->land_squash = 0;
    }

    /* ── Update traps ── */
    float total_t = (float)GetTime();
    for (int i = 0; i < lv->trap_count; i++) {
        Trap *t = &lv->traps[i];
        switch (t->type) {
            case TRAP_CRUSHER_V: {
                float spd = 2.5f;
                t->move_t += dt * spd * t->move_dir;
                if (t->move_t >= 1.0f) { t->move_t = 1.0f; t->move_dir = -1; }
                if (t->move_t <= 0.0f) { t->move_t = 0.0f; t->move_dir =  1; }
                float phase_offset = sinf(t->phase * 3.14159f * 2.0f) * 0.5f + 0.5f;
                float blend = sinf((t->move_t + phase_offset) * 3.14159f * 2.0f) * 0.5f + 0.5f;
                t->rect.x = t->origin.x;
                t->rect.y = t->origin.y + (t->target.y - t->origin.y) * blend;
                t->rect.width  = t->origin.width;
                t->rect.height = t->origin.height;
                break;
            }
            case TRAP_CRUSHER_H: {
                float spd = 2.0f;
                t->move_t += dt * spd * t->move_dir;
                if (t->move_t >= 1.0f) { t->move_t = 1.0f; t->move_dir = -1; }
                if (t->move_t <= 0.0f) { t->move_t = 0.0f; t->move_dir =  1; }
                float blend = sinf((t->move_t + t->phase) * 3.14159f) * 0.5f + 0.5f;
                t->rect.x = t->origin.x + (t->target.x - t->origin.x) * blend;
                t->rect.y = t->origin.y;
                t->rect.width  = t->origin.width;
                t->rect.height = t->origin.height;
                break;
            }
            case TRAP_FIRE: {
                /* Fire jets: active half the period */
                float period = 1.5f;
                float phase_t = fmodf(total_t + t->phase * period, period);
                t->active = (phase_t < period * 0.4f);
                if (t->active) {
                    /* extend upward when on */
                    t->rect.height = TILE + 20.0f * sinf(phase_t / (period*0.4f) * 3.14159f);
                } else {
                    t->rect.height = TILE * 0.3f;
                }
                break;
            }
            case TRAP_VANISH_FLOOR: {
                Rectangle pr = player_rect(p);
                if (t->active && CheckCollisionRecs(pr, t->rect)) {
                    t->timer += dt;
                    if (t->timer > 0.4f) t->active = false;
                }
                break;
            }
            default: break;
        }
    }

    /* ── Collision with traps ── */
    Rectangle pr = player_rect(p);
    for (int i = 0; i < lv->trap_count; i++) {
        Trap *t = &lv->traps[i];
        if (!t->active) continue;

        Rectangle tr = t->rect;

        switch (t->type) {
            case TRAP_FAKE_PLATFORM:
                /* Non-solid fake platform: player falls straight through. */
                break;

            case TRAP_SPIKE_UP:
            case TRAP_SPIKE_DOWN:
                if (CheckCollisionRecs(pr, tr)) kill_player(g);
                break;

            case TRAP_CRUSHER_V:
            case TRAP_CRUSHER_H:
            case TRAP_SAW:
                if (CheckCollisionRecs(pr, tr)) kill_player(g);
                break;

            case TRAP_FIRE:
                if (t->active && CheckCollisionRecs(pr, tr)) kill_player(g);
                break;

            case TRAP_VANISH_FLOOR:
                if (t->active && CheckCollisionRecs(pr, tr)) {
                    if (p->vel.y >= 0) {
                        p->pos.y  = tr.y - pr.height;
                        p->vel.y  = 0;
                        p->on_ground = true;
                    }
                }
                break;

            case TRAP_MAGNET: {
                /* pull player toward center */
                float cx = tr.x + tr.width/2 - p->size.x/2;
                float cy = tr.y + tr.height/2 - p->size.y/2;
                if (CheckCollisionRecs(pr, tr)) {
                    float dx = cx - p->pos.x;
                    p->vel.x += dx * 80.0f * dt;
                }
                break;
            }

            case TRAP_DECOY_DOOR:
                /* handled in door check below */
                break;
        }

        if (!p->alive) break;
    }

    if (!p->alive) return;

    /* ── Check door (real exit) ── */
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int v = lv->tiles[r][c];
            if (v == 2) { /* real door */
                Rectangle dr = {(float)(c*TILE), (float)(r*TILE), (float)TILE, (float)TILE};
                if (CheckCollisionRecs(pr, dr)) {
                    g->win_timer = 1.2f;
                    g->screen    = SCREEN_WIN_LEVEL;
                    add_particle(g, (Vector2){dr.x + TILE/2, dr.y + TILE/2},
                                 COL_DOOR, 250.0f, 40);
                }
            }
            if (v == 3) { /* decoy door → kill with dramatic explosion */
                Rectangle dr = {(float)(c*TILE), (float)(r*TILE), (float)TILE, (float)TILE};
                if (CheckCollisionRecs(pr, dr)) {
                    add_particle(g, (Vector2){dr.x + TILE/2, dr.y + TILE/2}, COL_DECOY, 300.0f, 50);
                    kill_player(g);
                }
            }
        }
    }

    /* ── Boundary kill ── */
    if (p->pos.x < 0 || p->pos.x + p->size.x > SCREEN_W ||
        p->pos.y < 0 || p->pos.y + p->size.y > SCREEN_H) {
        kill_player(g);
    }
}

/* ─────────────────────────── Draw helpers ─────────────────────────── */

static void draw_spikes(Rectangle r, TrapType type, bool is_adaptive) {
    int count = (int)(r.width / 12);
    if (count < 1) count = 1;
    float sw = r.width / count;
    Color col  = is_adaptive ? COL_ADAPTIVE : COL_SPIKE;
    Color col2 = is_adaptive ? (Color){255,160,200,255} : COL_SPIKE2;
    for (int i = 0; i < count; i++) {
        float x0 = r.x + i * sw;
        float x1 = r.x + (i + 0.5f) * sw;
        float x2 = r.x + (i + 1.0f) * sw;
        Vector2 pts[3];
        if (type == TRAP_SPIKE_UP) {
            pts[0] = (Vector2){x0, r.y + r.height};
            pts[1] = (Vector2){x1, r.y};
            pts[2] = (Vector2){x2, r.y + r.height};
        } else {
            pts[0] = (Vector2){x0, r.y};
            pts[1] = (Vector2){x1, r.y + r.height};
            pts[2] = (Vector2){x2, r.y};
        }
        DrawTriangle(pts[0], pts[1], pts[2], col);
        /* highlight edge */
        DrawLineEx(pts[0], pts[1], 1.5f, col2);
        DrawLineEx(pts[1], pts[2], 1.5f, col2);
    }
}

static void draw_saw(Rectangle r, float time_val, float phase) {
    float cx = r.x + r.width/2;
    float cy = r.y + r.height/2 + sinf(time_val * 3.0f + phase * 6.28f) * 6.0f;
    float radius = r.width / 2 * 0.85f;
    float angle = time_val * 5.0f + phase * 6.28f;
    int teeth = 10;
    DrawCircle((int)cx, (int)cy, radius, COL_SAW);
    for (int i = 0; i < teeth; i++) {
        float a0 = angle + (float)i / teeth * 6.28318f;
        float a1 = angle + ((float)i + 0.5f) / teeth * 6.28318f;
        float a2 = angle + ((float)i + 1.0f) / teeth * 6.28318f;
        Vector2 p0 = {cx + cosf(a0)*radius,        cy + sinf(a0)*radius};
        Vector2 p1 = {cx + cosf(a1)*(radius+8.0f), cy + sinf(a1)*(radius+8.0f)};
        Vector2 p2 = {cx + cosf(a2)*radius,        cy + sinf(a2)*radius};
        DrawTriangle(p0, p1, p2, (Color){255, 210, 80, 255});
    }
    DrawCircle((int)cx, (int)cy, radius * 0.3f, (Color){60, 50, 30, 255});
}

static void draw_crusher(Rectangle r, TrapType type) {
    Color c1 = COL_CRUSHER, c2 = COL_CRUSHER2;
    
    /* Draw thin laser warning guide lines */
    if (type == TRAP_CRUSHER_V) {
        DrawRectangle((int)r.x + (int)r.width/2 - 1, 0, 2, SCREEN_H, (Color){255, 0, 0, 35});
    } else {
        DrawRectangle(0, (int)r.y + (int)r.height/2 - 1, SCREEN_W, 2, (Color){255, 0, 0, 35});
    }

    DrawRectangleRec(r, c1);
    DrawRectangleLinesEx(r, 2, c2);
    /* bolts */
    int bx = (type == TRAP_CRUSHER_V) ? (int)(r.width / 12) : 2;
    int by = (type == TRAP_CRUSHER_V) ? 2 : (int)(r.height / 12);
    for (int i = 0; i < bx; i++)
        for (int j = 0; j < by; j++) {
            float px = r.x + (i+0.5f) * (r.width  / bx);
            float py = r.y + (j+0.5f) * (r.height / by);
            DrawCircle((int)px, (int)py, 3, c2);
        }
}

static void draw_fire(Rectangle r, bool active) {
    if (!active) {
        DrawRectangle((int)r.x, (int)(r.y + r.height * 0.7f),
                      (int)r.width, (int)(r.height * 0.3f), (Color){100,40,0,180});
        return;
    }
    float t = (float)GetTime();
    for (int layer = 0; layer < 3; layer++) {
        float shrink = layer * 0.15f;
        float flicker = sinf(t * 15.0f + layer * 2.1f) * 4.0f;
        DrawRectangle(
            (int)(r.x + shrink * r.width + flicker),
            (int)r.y,
            (int)(r.width * (1.0f - shrink * 2)),
            (int)r.height,
            (Color){255, (unsigned char)(120 - layer*40), (unsigned char)(layer*10), (unsigned char)(200 - layer*50)}
        );
    }
}

static void draw_fake_platform(Rectangle r, bool revealed) {
    if (!revealed) {
        /* Looks slightly different shade to trained eye but appears solid */
        DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)r.height, COL_FAKE);
        DrawRectangleLinesEx(r, 1, (Color){100, 160, 100, 180});
    } else {
        /* Revealed as fake: flickery red */
        float a = sinf((float)GetTime() * 20.0f);
        DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)r.height,
                      (Color){200, 50, 50, (unsigned char)(100 + 80 * a)});
    }
}

static void draw_door(float x, float y, bool is_decoy) {
    Color top  = is_decoy ? COL_DECOY : COL_DOOR;
    Color body = is_decoy ? (Color){160, 50, 50, 255} : COL_DOOR2;
    DrawRectangle((int)x, (int)y, TILE, TILE, body);
    DrawRectangle((int)x + 4, (int)y + 4, TILE - 8, TILE - 8, top);
    /* door frame */
    DrawRectangleLinesEx((Rectangle){x, y, TILE, TILE}, 2, top);
    /* knob */
    DrawCircle((int)(x + TILE - 8), (int)(y + TILE/2), 3,
               is_decoy ? (Color){255, 200, 200, 255} : (Color){200, 255, 200, 255});
    /* label */
    if (is_decoy) {
        DrawText("?", (int)x + TILE/2 - 5, (int)y + TILE/2 - 8, 16, (Color){255,180,180,255});
    } else {
        DrawText("->", (int)x + TILE/4, (int)y + TILE/2 - 6, 12, (Color){200,255,200,255});
    }
}

static void draw_level(Game *g) {
    Level *lv = &g->level;
    float t = (float)GetTime();

    /* Tiles */
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int v = lv->tiles[r][c];
            float x = (float)(c * TILE), y = (float)(r * TILE);
            if (v == 1) {
                DrawRectangle((int)x, (int)y, TILE, TILE, COL_TILE);
                /* subtle top edge highlight */
                DrawRectangle((int)x, (int)y, TILE, 2, COL_TILE_EDGE);
            } else if (v == 2) {
                draw_door(x, y, false);
            } else if (v == 3) {
                draw_door(x, y, true);
            }
        }
    }

    /* Traps */
    for (int i = 0; i < lv->trap_count; i++) {
        Trap *tr = &lv->traps[i];
        switch (tr->type) {
            case TRAP_SPIKE_UP:
            case TRAP_SPIKE_DOWN:
                draw_spikes(tr->rect, tr->type, tr->is_adaptive);
                break;
            case TRAP_CRUSHER_V:
            case TRAP_CRUSHER_H:
                if (tr->active) draw_crusher(tr->rect, tr->type);
                break;
            case TRAP_SAW:
                if (tr->active) draw_saw(tr->rect, t, tr->phase);
                break;
            case TRAP_FIRE:
                draw_fire(tr->rect, tr->active);
                break;
            case TRAP_FAKE_PLATFORM:
                draw_fake_platform(tr->rect, tr->revealed);
                break;
            case TRAP_VANISH_FLOOR:
                if (tr->active) {
                    float alpha = 1.0f - tr->timer / 0.4f;
                    DrawRectangle((int)tr->rect.x, (int)tr->rect.y,
                                  (int)tr->rect.width, (int)tr->rect.height,
                                  (Color){140, 80, 160, (unsigned char)(200 * alpha)});
                }
                break;
            case TRAP_MAGNET: {
                /* Draw magnet field hint */
                Color mc = COL_MAGNET;
                mc.a = 40;
                DrawRectangleRec(tr->rect, mc);
                DrawRectangleLinesEx(tr->rect, 1, COL_MAGNET);
                break;
            }
            case TRAP_DECOY_DOOR:
                draw_door(tr->rect.x, tr->rect.y, true);
                break;
        }
    }
}

static void draw_player(Game *g) {
    Player *p = &g->player;
    if (!p->alive) return;

    float t = (float)GetTime();
    bool  walking = (fabsf(p->vel.x) > 20.0f);
    float bob = walking ? sinf(t * 12.0f) * 2.5f : 0.0f;

    /* Dynamic Squash and Stretch scale factors */
    float sx = 1.0f;
    float sy = 1.0f;
    if (p->land_squash > 0) {
        sy = 1.0f - p->land_squash;
        sx = 1.0f + p->land_squash * 0.5f;
    } else if (!p->on_ground) {
        float factor = fminf(fabsf(p->vel.y) / 1200.0f, 0.25f);
        if (p->vel.y < 0) { // moving up (jumping)
            sy = 1.0f + factor;
            sx = 1.0f - factor * 0.5f;
        } else { // moving down (falling)
            sy = 1.0f + factor * 0.5f;
            sx = 1.0f - factor * 0.25f;
        }
    }

    int draw_w = (int)(p->size.x * sx);
    int draw_h = (int)(p->size.y * sy);
    int draw_x = (int)(p->pos.x + (p->size.x - draw_w) / 2);
    
    /* Anchor at top if gravity flipped, else anchor at bottom */
    int draw_y;
    if (g->gravity_dir) {
        draw_y = (int)(p->pos.y - bob);
    } else {
        draw_y = (int)(p->pos.y + (p->size.y - draw_h) + bob);
    }

    /* Shadow */
    DrawEllipse((int)(p->pos.x + p->size.x/2), (int)(p->pos.y + p->size.y + 2),
                (int)(p->size.x * 0.4f * sx), 3, (Color){0, 0, 0, 80});

    /* Body */
    DrawRectangle(draw_x, draw_y, draw_w, draw_h, COL_PLAYER2);
    DrawRectangle(draw_x + 2, draw_y + 2, draw_w - 4, draw_h / 2, COL_PLAYER);

    /* Eyes */
    int eye_y = g->gravity_dir ? (int)(draw_y + draw_h - 12) : (int)(draw_y + 8);
    int eye_x1 = draw_x + (int)(draw_w * 0.3f);
    int eye_x2 = draw_x + (int)(draw_w * 0.7f);
    DrawCircle(eye_x1, eye_y, 4 * sx, WHITE);
    DrawCircle(eye_x2, eye_y, 4 * sx, WHITE);
    
    int look_dir = (p->vel.x > 20.0f) ? 1 : ((p->vel.x < -20.0f) ? -1 : 0);
    DrawCircle(eye_x1 + look_dir, eye_y, 2 * sx, (Color){30, 20, 40, 255});
    DrawCircle(eye_x2 + look_dir, eye_y, 2 * sx, (Color){30, 20, 40, 255});

    /* Dash trail (2D offset) */
    if (p->dash_timer > 0) {
        for (int i = 1; i <= 4; i++) {
            float ox = -p->dash_dir.x * i * 8.0f;
            float oy = -p->dash_dir.y * i * 8.0f;
            int trail_x = (int)(draw_x + ox);
            int trail_y = (int)(draw_y + oy);
            DrawRectangle(trail_x, trail_y, draw_w, draw_h,
                          (Color){255, 220, 80, (unsigned char)(60 - i*12)});
        }
    }

    /* Gravity flip indicator */
    if (g->gravity_dir) {
        DrawText("v", (int)(p->pos.x + 8), (int)(p->pos.y - 16), 12, COL_GRAV);
    }
}

static void draw_hud(Game *g) {
    Level  *lv = &g->level;
    Player *p  = &g->player;

    /* Level name */
    DrawText(lv->name, 10, 10, 18, COL_TEXT);

    /* Death counter */
    char buf[64];
    snprintf(buf, sizeof(buf), "Deaths: %d", p->deaths);
    DrawText(buf, SCREEN_W - 120, 10, 16, COL_TEXT2);

    /* Total deaths */
    snprintf(buf, sizeof(buf), "Total: %d", g->total_deaths);
    DrawText(buf, SCREEN_W - 120, 30, 14, COL_TEXT2);

    /* Dash cooldown bar */
    if (p->dash_cooldown > 0) {
        float frac = 1.0f - (p->dash_cooldown / DASH_COOLDOWN);
        DrawRectangle(10, SCREEN_H - 24, 80, 8, (Color){60, 60, 80, 200});
        DrawRectangle(10, SCREEN_H - 24, (int)(80 * frac), 8, COL_PLAYER);
        DrawText("DASH", 95, SCREEN_H - 26, 12, COL_TEXT2);
    } else {
        DrawText("DASH READY", 10, SCREEN_H - 26, 12, COL_PLAYER);
    }

    /* Time warp indicator */
    if (lv->time_warp) {
        float ts = lv->time_scale;
        Color tc = (ts > 1.0f) ? COL_FIRE : COL_GRAV;
        snprintf(buf, sizeof(buf), "TIME x%.1f", ts);
        DrawText(buf, SCREEN_W/2 - 40, 10, 16, tc);
    }

    /* Gravity flip warning */
    if (lv->gravity_flip_enabled) {
        float remaining = lv->gravity_flip_interval - lv->gravity_timer;
        if (remaining < 1.5f) {
            float blink = sinf((float)GetTime() * 15.0f);
            if (blink > 0) {
                DrawText("GRAVITY FLIP!", SCREEN_W/2 - 70, SCREEN_H/2 - 60, 22, COL_GRAV);
            }
        }
        snprintf(buf, sizeof(buf), "Flip in: %.1fs", remaining);
        DrawText(buf, SCREEN_W/2 - 50, 10, 14, COL_GRAV);
    }

    /* Mirror warning */
    if (lv->mirror_mode) {
        float remaining = lv->mirror_interval - lv->mirror_timer;
        if (remaining < 1.0f) {
            float blink = sinf((float)GetTime() * 12.0f);
            if (blink > 0) DrawText("MIRROR!", SCREEN_W/2 - 40, SCREEN_H/2 - 40, 20, COL_WARN);
        }
    }

    /* Hint at start */
    if (lv->hint[0]) {
        DrawText(lv->hint, SCREEN_W/2 - MeasureText(lv->hint, 14)/2,
                 SCREEN_H - 40, 14, (Color){160, 150, 180, 180});
    }

    /* Large Level Title Card Overlay Banner */
    if (g->level_title_timer > 0) {
        float alpha = g->level_title_timer > 0.5f ? 1.0f : (g->level_title_timer / 0.5f);
        Color bg_col = (Color){0, 0, 0, (unsigned char)(180 * alpha)};
        Color text_col = (Color){255, 255, 255, (unsigned char)(255 * alpha)};
        Color hint_col = (Color){200, 180, 255, (unsigned char)(200 * alpha)};
        
        DrawRectangle(0, SCREEN_H/2 - 60, SCREEN_W, 120, bg_col);
        DrawRectangleLinesEx((Rectangle){0, SCREEN_H/2 - 60, SCREEN_W, 120}, 2.0f, (Color){160, 120, 220, (unsigned char)(180 * alpha)});
        DrawText(lv->name, SCREEN_W/2 - MeasureText(lv->name, 28)/2, SCREEN_H/2 - 35, 28, text_col);
        DrawText(lv->hint, SCREEN_W/2 - MeasureText(lv->hint, 16)/2, SCREEN_H/2 + 15, 16, hint_col);
    }
}

/* ─────────────────────────── Screens ─────────────────────────── */

static void draw_menu(Game *g) {
    float t = (float)GetTime();

    /* Animated background */
    for (int i = 0; i < 20; i++) {
        int x = (int)(sinf(t * 0.3f + i * 1.7f) * SCREEN_W * 0.4f + SCREEN_W * 0.5f);
        int y = (int)(cosf(t * 0.2f + i * 2.3f) * SCREEN_H * 0.4f + SCREEN_H * 0.5f);
        DrawCircle(x, y, 2 + i % 3, (Color){80, 50, 100, 60});
    }

    /* Title */
    const char *title = "DEVIL'S GAUNTLET";
    int tw = MeasureText(title, 48);
    float shake = sinf(t * 2.0f) * 2.0f;
    DrawText(title, SCREEN_W/2 - tw/2 + (int)shake, 120, 48, (Color){255, 80, 80, 255});
    DrawText(title, SCREEN_W/2 - tw/2, 120, 48, COL_TEXT);

    const char *sub = "A Devious Trap Platformer";
    DrawText(sub, SCREEN_W/2 - MeasureText(sub, 18)/2, 178, 18, COL_TEXT2);

    /* Menu items */
    const char *items[] = {"PLAY", "QUIT"};
    for (int i = 0; i < 2; i++) {
        bool sel = (g->selected_menu == i);
        int y = 280 + i * 60;
        if (sel) {
            DrawRectangle(SCREEN_W/2 - 90, y - 6, 180, 40, (Color){80, 50, 100, 200});
            DrawRectangleLinesEx((Rectangle){SCREEN_W/2 - 90, y - 6, 180, 40}, 1,
                                  (Color){160, 120, 200, 255});
        }
        DrawText(items[i], SCREEN_W/2 - MeasureText(items[i], 24)/2, y, 24,
                 sel ? COL_TEXT : COL_TEXT2);
    }

    /* Controls */
    DrawText("WASD/Arrows: Move   Space: Jump   Shift: Dash   R: Restart",
             SCREEN_W/2 - 260, SCREEN_H - 50, 14, COL_TEXT2);

    /* Navigate */
    if (IsKeyPressed(KEY_UP)   || IsKeyPressed(KEY_W)) g->selected_menu = (g->selected_menu - 1 + 2) % 2;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) g->selected_menu = (g->selected_menu + 1) % 2;
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        if (g->selected_menu == 0) {
            g->current_level  = 0;
            g->total_deaths   = 0;
            load_level(g, 0);
            g->screen = SCREEN_PLAY;
        } else {
            CloseWindow();
        }
    }
}

static void draw_dead_screen(Game *g) {
    /* Semi-transparent overlay */
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){40, 10, 20, 180});

    const char *msg = "YOU DIED";
    float shake = sinf((float)GetTime() * 20.0f) * 3.0f;
    DrawText(msg, SCREEN_W/2 - MeasureText(msg, 48)/2 + (int)shake,
             SCREEN_H/2 - 80, 48, (Color){255, 60, 60, 255});

    char buf[64];
    snprintf(buf, sizeof(buf), "Deaths this level: %d", g->player.deaths);
    DrawText(buf, SCREEN_W/2 - MeasureText(buf, 20)/2, SCREEN_H/2 - 10, 20, COL_TEXT2);

    if (g->player.death_mem_count > 0) {
        DrawText("New traps await...", SCREEN_W/2 - 90, SCREEN_H/2 + 20, 16, COL_ADAPTIVE);
    }

    DrawText("Press SPACE to try again  |  ESC for menu",
             SCREEN_W/2 - 190, SCREEN_H/2 + 60, 16, COL_TEXT2);

    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) respawn(g);
    if (IsKeyPressed(KEY_ESCAPE)) g->screen = SCREEN_MENU;
}

static void draw_win_level(Game *g) {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){10, 40, 20, 180});

    const char *msg = "LEVEL CLEAR!";
    DrawText(msg, SCREEN_W/2 - MeasureText(msg, 40)/2, SCREEN_H/2 - 60, 40, COL_DOOR);

    char buf[64];
    snprintf(buf, sizeof(buf), "Deaths: %d", g->player.deaths);
    DrawText(buf, SCREEN_W/2 - MeasureText(buf, 20)/2, SCREEN_H/2, 20, COL_TEXT2);

    if (g->current_level < MAX_LEVELS - 1) {
        DrawText("SPACE / ENTER — Next Level", SCREEN_W/2 - 130, SCREEN_H/2 + 40, 18, COL_TEXT);
        if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
            g->current_level++;
            load_level(g, g->current_level);
            g->screen = SCREEN_PLAY;
        }
    } else {
        g->screen = SCREEN_WIN_GAME;
    }
    if (IsKeyPressed(KEY_ESCAPE)) g->screen = SCREEN_MENU;
}

static void draw_win_game(Game *g) {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){10, 10, 40, 200});

    float t = (float)GetTime();
    for (int i = 0; i < 30; i++) {
        int x = GetRandomValue(0, SCREEN_W);
        int y = GetRandomValue(0, SCREEN_H);
        DrawCircle(x, y, 2, COL_PLAYER);
    }

    const char *msg1 = "YOU BEAT THE DEVIL!";
    const char *msg2 = "Congratulations!";
    char buf[64];
    snprintf(buf, sizeof(buf), "Total Deaths: %d", g->total_deaths);

    DrawText(msg1, SCREEN_W/2 - MeasureText(msg1, 40)/2,
             SCREEN_H/2 - 80 + (int)(sinf(t*2)*4), 40, COL_PLAYER);
    DrawText(msg2, SCREEN_W/2 - MeasureText(msg2, 24)/2, SCREEN_H/2 - 20, 24, COL_TEXT);
    DrawText(buf,  SCREEN_W/2 - MeasureText(buf, 20)/2,  SCREEN_H/2 + 20, 20, COL_TEXT2);

    const char *hint = "The devil respects your persistence.";
    DrawText(hint, SCREEN_W/2 - MeasureText(hint, 16)/2, SCREEN_H/2 + 60, 16, COL_TEXT2);

    DrawText("ENTER — Play Again    ESC — Menu",
             SCREEN_W/2 - 155, SCREEN_H/2 + 100, 16, COL_TEXT2);

    if (IsKeyPressed(KEY_ENTER)) {
        g->current_level = 0;
        g->total_deaths  = 0;
        load_level(g, 0);
        g->screen = SCREEN_PLAY;
    }
    if (IsKeyPressed(KEY_ESCAPE)) g->screen = SCREEN_MENU;
}

/* ─────────────────────────── Main ─────────────────────────── */

int main(void) {
    InitWindow(SCREEN_W, SCREEN_H, "Devil's Gauntlet");
    SetTargetFPS(60);
    SetRandomSeed((unsigned int)GetTime());

    Game g = {0};
    g.screen = SCREEN_MENU;
    g.selected_menu = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f; /* cap at 50ms */

        /* Global keys */
        if (g.screen == SCREEN_PLAY) {
            if (IsKeyPressed(KEY_R)) {
                load_level(&g, g.current_level);
                g.screen = SCREEN_PLAY;
            }
            if (IsKeyPressed(KEY_ESCAPE)) g.screen = SCREEN_MENU;
        }

        /* Dead timer auto-advance */
        if (g.screen == SCREEN_DEAD) {
            g.dead_timer -= dt;
            update_particles(&g, dt);
            if (g.dead_timer <= 0) {
                respawn(&g);
            }
        }

        /* Win level timer */
        if (g.screen == SCREEN_WIN_LEVEL) {
            g.win_timer -= dt;
            update_particles(&g, dt);
            if (g.win_timer <= 0) {
                if (g.current_level < MAX_LEVELS - 1) {
                    g.current_level++;
                    load_level(&g, g.current_level);
                    g.screen = SCREEN_PLAY;
                } else {
                    g.screen = SCREEN_WIN_GAME;
                }
            }
        }

        /* Game update */
        if (g.screen == SCREEN_PLAY && g.player.alive) {
            update_game(&g, dt);
            update_particles(&g, dt);
        }

        /* ── Draw ── */
        BeginDrawing();
        ClearBackground(COL_BG);

        /* Camera shake offset */
        int sx = 0, sy = 0;
        if (g.camera_shake > 0) {
            sx = GetRandomValue(-1, 1) * (int)(g.camera_shake * 10);
            sy = GetRandomValue(-1, 1) * (int)(g.camera_shake * 10);
        }
        if (sx || sy) {
            BeginScissorMode(0, 0, SCREEN_W, SCREEN_H);
            /* translate manually by drawing everything offset */
        }

        switch (g.screen) {
            case SCREEN_MENU:
                draw_menu(&g);
                break;

            case SCREEN_PLAY:
                /* Background grid hint */
                for (int r = 0; r < ROWS; r++)
                    for (int c = 0; c < COLS; c++)
                        DrawRectangle(c*TILE, r*TILE, TILE-1, TILE-1,
                                      (Color){20, 16, 28, 255});
                draw_level(&g);
                draw_player(&g);
                draw_particles(&g);
                draw_hud(&g);
                break;

            case SCREEN_DEAD:
                /* Show the level behind */
                for (int r = 0; r < ROWS; r++)
                    for (int c = 0; c < COLS; c++)
                        DrawRectangle(c*TILE, r*TILE, TILE-1, TILE-1,
                                      (Color){20, 16, 28, 255});
                draw_level(&g);
                draw_particles(&g);
                if (g.dead_timer <= 0) draw_dead_screen(&g);
                break;

            case SCREEN_WIN_LEVEL:
                for (int r = 0; r < ROWS; r++)
                    for (int c = 0; c < COLS; c++)
                        DrawRectangle(c*TILE, r*TILE, TILE-1, TILE-1,
                                      (Color){20, 16, 28, 255});
                draw_level(&g);
                draw_particles(&g);
                draw_win_level(&g);
                break;

            case SCREEN_WIN_GAME:
                draw_win_game(&g);
                break;
        }

        if (sx || sy) EndScissorMode();

        /* Death flash */
        if (g.flash_alpha > 0) {
            DrawRectangle(0, 0, SCREEN_W, SCREEN_H,
                          (Color){255, 80, 80, (unsigned char)(g.flash_alpha * 180)});
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}