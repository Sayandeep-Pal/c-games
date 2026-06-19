/*
 * CYBER COMMANDO
 * A side-scrolling run-and-gun platformer in C + Raylib (Contra style)
 *
 * Compile: gcc commando.c -o commando -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
 *
 * Controls:
 *   A / D or LEFT / RIGHT - Move Left / Right
 *   W / S or UP / DOWN     - Aim Up / Aim Down (Down in mid-air only)
 *   SPACE                  - Jump
 *   J or X                 - Shoot (Hold to autofire)
 *   K or Z                 - Dash
 *   R                      - Restart (on Game Over)
 *   ESC                    - Quit to Menu
 */

#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─────────────────────────── Constants ─────────────────────────── */

#define SCREEN_W        1024
#define SCREEN_H        720
#define GRAVITY         1600.0f
#define JUMP_FORCE      -620.0f
#define MOVE_SPEED      280.0f
#define MAX_PLATFORMS   64
#define MAX_BULLETS     128
#define MAX_ENEMY_BULS  128
#define MAX_ENEMIES     32
#define MAX_PARTICLES   256
#define MAX_CAPSULES    4
#define LEVEL_COUNT     3

/* ─────────────────────────── Enums ─────────────────────────── */

typedef enum {
    SCREEN_MENU,
    SCREEN_PLAY,
    SCREEN_GAMEOVER,
    SCREEN_WIN
} GameScreen;

typedef enum {
    WEAPON_RIFLE,
    WEAPON_SPREAD,
    WEAPON_LASER,
    WEAPON_FLAME
} WeaponType;

typedef enum {
    ENEMY_RUNNER,
    ENEMY_SNIPER,
    ENEMY_TURRET,
    ENEMY_BOSS_LEFT_TURRET,
    ENEMY_BOSS_RIGHT_TURRET,
    ENEMY_BOSS_CORE
} EnemyType;

typedef enum {
    PARTICLE_SPARK,
    PARTICLE_FIRE,
    PARTICLE_CASING,
    PARTICLE_DUST
} ParticleType;

/* ─────────────────────────── Structs ─────────────────────────── */

typedef struct {
    Vector2 pos;
    Vector2 vel;
    Vector2 size;
    bool    on_ground;
    bool    facing_right;
    int     lives;
    float   shield;
    float   max_shield;
    WeaponType weapon;
    float   shoot_cooldown;
    float   invuln_timer;
    float   dash_timer;
    float   dash_cooldown;
    Vector2 dash_dir;
    bool    alive;

    // Juice additions
    float   jump_rotation;
    float   squash_x;
    float   squash_y;
    Vector2 dash_ghosts[3];
    int     dash_ghost_count;
    float   dash_ghost_timer;

    // Boundary / checkpoint safety
    Vector2 last_safe_ground_pos;
} Player;

typedef struct {
    Rectangle rect;
    Color     color;
    bool      is_hazard;
} Platform;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    WeaponType type;
    float   angle;
    float   timer;
    int     damage;
    bool    active;
} Bullet;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    bool    active;
} EnemyBullet;

typedef struct {
    EnemyType type;
    Vector2 pos;
    Vector2 vel;
    Vector2 size;
    float   health;
    float   max_health;
    float   shoot_timer;
    bool    active;
    bool    facing_right;
    bool    boss_core_open; /* Only for boss core */

    // Juice additions
    float   hit_flash;
} Enemy;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    WeaponType drops_weapon;
    bool    active;
} Capsule;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    Color   color;
    float   life;
    float   max_life;
    float   size;
    ParticleType type;
} Particle;

typedef struct {
    int      index;
    float    length;
    Platform platforms[MAX_PLATFORMS];
    int      platform_count;
    Vector2  spawn_point;
    Vector2  boss_trigger_point;
    char     name[64];
    Color    bg_color;
    Color    platform_color;
} Level;

typedef struct {
    Player      player;
    Level       levels[LEVEL_COUNT];
    int         current_level;
    Bullet      bullets[MAX_BULLETS];
    EnemyBullet enemy_bullets[MAX_ENEMY_BULS];
    Enemy       enemies[MAX_ENEMIES];
    Capsule     capsules[MAX_CAPSULES];
    Particle    particles[MAX_PARTICLES];
    int         particle_count;
    GameScreen  screen;
    Camera2D    camera;
    float       camera_shake;
    bool        boss_active;
    float       boss_intro_timer;
    int         kill_count;
} GameState;

/* ─────────────────────────── Global Palette ─────────────────────────── */

#define COL_SKY          (Color){ 10, 10, 18, 255 }
#define COL_CYBER_PURPLE (Color){ 120, 40, 200, 255 }
#define COL_NEON_GREEN   (Color){ 50, 255, 120, 255 }
#define COL_NEON_BLUE    (Color){ 0, 220, 255, 255 }
#define COL_NEON_PINK    (Color){ 255, 40, 140, 255 }
#define COL_NEON_ORANGE  (Color){ 255, 120, 20, 255 }

/* ─────────────────────────── Procedural Audio Synth ─────────────────────────── */

typedef struct {
    float phase;
    float freq_start;
    float freq_end;
    float length;
    float volume;
    float time;
    bool active;
    bool noise;
} SynthSFX;

#define MAX_SFX_CHANNELS 8
static SynthSFX sfx_channels[MAX_SFX_CHANNELS] = { 0 };

static float seq_tempo = 130.0f; // synth tempo BPM
static int bass_notes[16] = { 36, 36, 39, 39, 41, 41, 44, 43, 36, 36, 39, 39, 41, 41, 46, 45 };
static int audio_tick = 0;

static void PlaySynthSFX(float freq_start, float freq_end, float length, float volume, bool noise) {
    for (int i = 0; i < MAX_SFX_CHANNELS; i++) {
        if (!sfx_channels[i].active) {
            sfx_channels[i].phase = 0.0f;
            sfx_channels[i].time = 0.0f;
            sfx_channels[i].freq_start = freq_start;
            sfx_channels[i].freq_end = freq_end;
            sfx_channels[i].length = length;
            sfx_channels[i].volume = volume;
            sfx_channels[i].noise = noise;
            sfx_channels[i].active = true;
            return;
        }
    }
}

// Background music and SFX generation callback
void SynthAudioCallback(void *buffer, unsigned int frames) {
    short *out = (short *)buffer;
    float sample_rate = 44100.0f;
    static float bass_phase = 0.0f;
    static float filter_val = 0.0f;
    
    int samples_per_step = (int)(sample_rate * (60.0f / seq_tempo) / 4.0f); // 16th notes
    
    for (unsigned int i = 0; i < frames; i++) {
        int step = (audio_tick / samples_per_step) % 16;
        int sub_step = audio_tick % samples_per_step;
        audio_tick++;
        
        // Bass oscillator freq
        int midi_note = bass_notes[step];
        float freq = 440.0f * powf(2.0f, (midi_note - 69.0f) / 12.0f);
        
        // Sawtooth osc
        bass_phase += freq / sample_rate;
        if (bass_phase >= 1.0f) bass_phase -= 1.0f;
        float osc = 2.0f * bass_phase - 1.0f;
        
        float step_t = (float)sub_step / samples_per_step;
        float env = expf(-6.0f * step_t);
        
        // LPF filter sweep
        float target_cutoff = 0.04f + 0.16f * env;
        filter_val += (osc - filter_val) * target_cutoff;
        float bass_out = filter_val * env * 0.35f;
        
        // Cyber kick drum on beat
        float kick = 0.0f;
        if (step % 4 == 0) {
            float kick_t = (float)sub_step / samples_per_step;
            if (kick_t < 0.4f) {
                float kick_freq = 160.0f * expf(-24.0f * kick_t) + 38.0f;
                static float kick_phase = 0.0f;
                kick_phase += kick_freq / sample_rate;
                kick = sinf(2.0f * PI * kick_phase) * expf(-9.0f * kick_t) * 0.75f;
            }
        }
        
        // Snare / Hi-hat noises
        float perc = 0.0f;
        if (step % 8 == 4) { // snare
            float snare_t = (float)sub_step / samples_per_step;
            float snare_env = expf(-14.0f * snare_t);
            perc += ((float)rand() / RAND_MAX * 2.0f - 1.0f) * snare_env * 0.18f;
        }
        if (step % 2 == 1) { // hi-hat
            float hh_t = (float)sub_step / samples_per_step;
            float hh_env = expf(-40.0f * hh_t);
            perc += ((float)rand() / RAND_MAX * 2.0f - 1.0f) * hh_env * 0.1f;
        }
        
        float music_mix = bass_out + kick + perc;
        
        // SFX channels mixing
        float sfx_mix = 0.0f;
        for (int ch = 0; ch < MAX_SFX_CHANNELS; ch++) {
            if (!sfx_channels[ch].active) continue;
            
            float t = sfx_channels[ch].time;
            float dur = sfx_channels[ch].length;
            
            if (t >= dur) {
                sfx_channels[ch].active = false;
                continue;
            }
            
            float progress = t / dur;
            float sfx_env = expf(-6.0f * progress);
            
            float val = 0.0f;
            if (sfx_channels[ch].noise) {
                val = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * sfx_env * sfx_channels[ch].volume;
            } else {
                float sfx_freq = sfx_channels[ch].freq_start + progress * (sfx_channels[ch].freq_end - sfx_channels[ch].freq_start);
                sfx_channels[ch].phase += sfx_freq / sample_rate;
                if (sfx_channels[ch].phase >= 1.0f) sfx_channels[ch].phase -= 1.0f;
                val = sinf(2.0f * PI * sfx_channels[ch].phase) * sfx_env * sfx_channels[ch].volume;
            }
            
            sfx_mix += val;
            sfx_channels[ch].time += 1.0f / sample_rate;
        }
        
        float final_mix = music_mix * 0.35f + sfx_mix * 0.65f;
        if (final_mix > 1.0f) final_mix = 1.0f;
        if (final_mix < -1.0f) final_mix = -1.0f;
        
        out[i] = (short)(final_mix * 11000.0f);
    }
}

/* ─────────────────────────── Global Sprites ─────────────────────────── */

static Texture2D texPlayer;
static Texture2D texRunner;
static Texture2D texSniper;
static Texture2D texTurret;
static Texture2D texPlayerSheet;
static Texture2D texTileset;
static Texture2D texBackground;

Texture2D GeneratePlayerTexture() {
    Image img = GenImageColor(64, 64, BLANK);
    
    // Draw neon blue armored cyber suit
    ImageDrawCircle(&img, 32, 32, 22, (Color){ 0, 100, 200, 255 }); // shadow base
    ImageDrawCircle(&img, 32, 32, 18, (Color){ 0, 180, 255, 255 }); // body base
    
    // Chest piece detail
    ImageDrawRectangle(&img, 24, 26, 16, 12, (Color){ 120, 240, 255, 255 });
    ImageDrawRectangle(&img, 28, 28, 8, 8, (Color){ 0, 180, 255, 255 });
    
    // Shoulder pads
    ImageDrawCircle(&img, 16, 32, 6, (Color){ 0, 100, 200, 255 });
    ImageDrawCircle(&img, 48, 32, 6, (Color){ 0, 100, 200, 255 });
    
    // Helmet
    ImageDrawCircle(&img, 32, 16, 10, (Color){ 0, 180, 255, 255 });
    // Glowing pink/neon visor
    ImageDrawRectangle(&img, 26, 14, 12, 4, COL_NEON_PINK);
    
    // Belt / straps
    ImageDrawRectangle(&img, 20, 40, 24, 4, COL_CYBER_PURPLE);
    
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

Texture2D GenerateRunnerTexture() {
    Image img = GenImageColor(64, 64, BLANK);
    
    // Red cyber-runner armored suit
    ImageDrawCircle(&img, 32, 32, 20, (Color){ 160, 30, 30, 255 });
    ImageDrawCircle(&img, 32, 32, 16, (Color){ 220, 60, 60, 255 });
    
    // Visor
    ImageDrawRectangle(&img, 26, 26, 12, 3, COL_NEON_BLUE);
    
    // Metal chest core
    ImageDrawCircle(&img, 32, 36, 5, (Color){ 80, 80, 90, 255 });
    
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

Texture2D GenerateSniperTexture() {
    Image img = GenImageColor(64, 64, BLANK);
    
    // Purple elite sniper suit
    ImageDrawCircle(&img, 32, 32, 20, (Color){ 120, 30, 120, 255 });
    ImageDrawCircle(&img, 32, 32, 16, (Color){ 180, 60, 180, 255 });
    
    // Glowing green visor
    ImageDrawRectangle(&img, 26, 26, 12, 3, COL_NEON_GREEN);
    
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

Texture2D GenerateTurretTexture() {
    Image img = GenImageColor(64, 64, BLANK);
    
    // Mechanical wall turret base
    ImageDrawCircle(&img, 32, 32, 24, (Color){ 60, 60, 70, 255 });
    ImageDrawCircle(&img, 32, 32, 16, (Color){ 100, 100, 110, 255 });
    
    // Core lens
    ImageDrawCircle(&img, 32, 32, 8, COL_NEON_ORANGE);
    
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

/* ─────────────────────────── Forward Declarations ─────────────────────────── */

static void InitGame(GameState *g);
static void LoadLevel(GameState *g, int level_idx);
static void UpdateGame(GameState *g, float dt);
static void DrawGame(GameState *g);
static void AddParticleEx(GameState *g, Vector2 pos, Color col, float speed, float size, ParticleType type, float custom_life, int count);
static void AddParticle(GameState *g, Vector2 pos, Color col, float speed, float size, int count);
static void SpawnEnemy(GameState *g, EnemyType type, float x, float y);
static void SpawnCapsule(GameState *g, float x, float y, WeaponType drops);
static void SpawnCasing(GameState *g, Vector2 pos, bool facing_right);
static void SpawnDust(GameState *g, Vector2 pos, int count);
static bool CheckTileCollision(Level *lv, Vector2 pos, Vector2 size, float *push_x, float *push_y, bool *hazard_hit);
static bool CheckTileCollisionX(Level *lv, Vector2 pos, Vector2 size, float *push_x, bool *hazard_hit);
static bool CheckTileCollisionY(Level *lv, Vector2 pos, Vector2 size, float *push_y, bool *hazard_hit);
static bool CheckRayCollisionRec2D(Vector2 start, Vector2 dir, Rectangle rect, float *t_collision);
static bool IsEnemyInAimLine(GameState *g, Vector2 spawn_pos, Vector2 aim);

/* ─────────────────────────── Level Builder ─────────────────────────── */

static void AddPlatform(Level *lv, float x, float y, float w, float h, Color col, bool is_hazard) {
    if (lv->platform_count >= MAX_PLATFORMS) return;
    Platform p = { (Rectangle){x, y, w, h}, col, is_hazard };
    lv->platforms[lv->platform_count++] = p;
}

static void LoadLevel(GameState *g, int level_idx) {
    g->current_level = level_idx;
    Level *lv = &g->levels[level_idx];
    memset(lv, 0, sizeof(Level));
    lv->index = level_idx;

    // Reset arrays
    memset(g->bullets, 0, sizeof(g->bullets));
    memset(g->enemy_bullets, 0, sizeof(g->enemy_bullets));
    memset(g->enemies, 0, sizeof(g->enemies));
    memset(g->capsules, 0, sizeof(g->capsules));
    g->particle_count = 0;
    g->boss_active = false;

    switch (level_idx) {
        case 0: {
            // STAGE 1: CYBER OUTPOST
            // Dense jungle outpost with vertical platforming, gaps, hazards, and high enemy count
            strcpy(lv->name, "STAGE 1: CYBER OUTPOST");
            lv->length = 4000.0f;
            lv->spawn_point = (Vector2){ 80, 450 };
            lv->boss_trigger_point = (Vector2){ 0, 0 };
            lv->bg_color = (Color){ 12, 18, 15, 255 };
            lv->platform_color = COL_NEON_GREEN;

            // Section 1: Starting grounds with low platforms
            AddPlatform(lv, 0, 520, 500, 80, COL_NEON_GREEN, false);
            AddPlatform(lv, 200, 400, 160, 20, COL_NEON_BLUE, false);  // starter high platform
            AddPlatform(lv, 420, 320, 120, 20, COL_NEON_BLUE, false);

            // Hazard pit 1
            AddPlatform(lv, 500, 560, 120, 40, COL_NEON_PINK, true);

            // Section 2: Ascending terrain
            AddPlatform(lv, 620, 500, 400, 100, COL_NEON_GREEN, false);
            AddPlatform(lv, 700, 360, 180, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 950, 280, 150, 20, COL_NEON_BLUE, false);

            // Hazard pit 2
            AddPlatform(lv, 1020, 560, 100, 40, COL_NEON_PINK, true);

            // Section 3: Mid-level gauntlet with multiple height platforms
            AddPlatform(lv, 1120, 520, 700, 80, COL_NEON_GREEN, false);
            AddPlatform(lv, 1200, 380, 200, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 1450, 280, 180, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 1600, 400, 150, 20, COL_NEON_BLUE, false);

            // Hazard pit 3
            AddPlatform(lv, 1820, 560, 130, 40, COL_NEON_PINK, true);

            // Section 4: Tight corridor with turret zone
            AddPlatform(lv, 1950, 520, 600, 80, COL_NEON_GREEN, false);
            AddPlatform(lv, 2050, 360, 150, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 2300, 260, 180, 20, COL_NEON_BLUE, false);

            // Hazard pit 4
            AddPlatform(lv, 2550, 560, 120, 40, COL_NEON_PINK, true);

            // Section 5: Final approach
            AddPlatform(lv, 2670, 520, 500, 80, COL_NEON_GREEN, false);
            AddPlatform(lv, 2800, 400, 160, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 3050, 300, 200, 20, COL_NEON_BLUE, false);

            // Hazard pit 5
            AddPlatform(lv, 3170, 560, 100, 40, COL_NEON_PINK, true);

            // Final stretch to exit
            AddPlatform(lv, 3270, 520, 730, 80, COL_NEON_GREEN, false);
            AddPlatform(lv, 3400, 380, 180, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 3650, 280, 160, 20, COL_NEON_BLUE, false);

            // Capsules
            SpawnCapsule(g, 700, 150, WEAPON_SPREAD);
            SpawnCapsule(g, 1500, 100, WEAPON_LASER);
            SpawnCapsule(g, 2800, 200, WEAPON_FLAME);

            // Enemies - first wave
            SpawnEnemy(g, ENEMY_SNIPER, 450, 272);   // on platform y=320 (x:420-540)
            SpawnEnemy(g, ENEMY_RUNNER, 300, 350);

            // Enemies - second wave
            SpawnEnemy(g, ENEMY_TURRET, 780, 328);   // on platform y=360 (x:700-880)
            SpawnEnemy(g, ENEMY_SNIPER, 1000, 232);  // on platform y=280 (x:950-1100)
            SpawnEnemy(g, ENEMY_RUNNER, 1300, 330);

            // Enemies - third wave (gauntlet)
            SpawnEnemy(g, ENEMY_SNIPER, 1550, 232);  // on platform y=280 (x:1450-1630)
            SpawnEnemy(g, ENEMY_TURRET, 1700, 368);  // on platform y=400 (x:1600-1750)
            SpawnEnemy(g, ENEMY_RUNNER, 1800, 470);

            // Enemies - fourth wave
            SpawnEnemy(g, ENEMY_TURRET, 2100, 328);  // on platform y=360 (x:2050-2200)
            SpawnEnemy(g, ENEMY_SNIPER, 2350, 212);  // on platform y=260 (x:2300-2480)
            SpawnEnemy(g, ENEMY_RUNNER, 2400, 470);
            SpawnEnemy(g, ENEMY_RUNNER, 2500, 470);

            // Enemies - final approach
            SpawnEnemy(g, ENEMY_TURRET, 2900, 368);  // on platform y=400 (x:2800-2960)
            SpawnEnemy(g, ENEMY_SNIPER, 3100, 252);  // on platform y=300 (x:3050-3250)
            SpawnEnemy(g, ENEMY_RUNNER, 3500, 470);
            SpawnEnemy(g, ENEMY_SNIPER, 3700, 232);  // on platform y=280 (x:3650-3810)
            break;
        }

        case 1: {
            // STAGE 2: REACTOR REFINERY
            // Vertical industrial platforming with moving hazards and heavy resistance
            strcpy(lv->name, "STAGE 2: REACTOR REFINERY");
            lv->length = 4200.0f;
            lv->spawn_point = (Vector2){ 80, 450 };
            lv->boss_trigger_point = (Vector2){ 0, 0 };
            lv->bg_color = (Color){ 20, 15, 10, 255 };
            lv->platform_color = COL_NEON_ORANGE;

            // Section 1: Stepped platforms ascending
            AddPlatform(lv, 0, 520, 350, 80, COL_NEON_ORANGE, false);
            AddPlatform(lv, 100, 400, 160, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 250, 300, 120, 20, COL_NEON_BLUE, false);

            // Hazard pit 1
            AddPlatform(lv, 350, 570, 100, 30, COL_NEON_PINK, true);

            // Section 2: Mid platforms and catwalks
            AddPlatform(lv, 450, 500, 500, 100, COL_NEON_ORANGE, false);
            AddPlatform(lv, 550, 350, 180, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 700, 220, 160, 20, COL_NEON_BLUE, false);

            // Hazard pit 2
            AddPlatform(lv, 950, 570, 120, 30, COL_NEON_PINK, true);

            // Section 3: Vertical shaft gauntlet
            AddPlatform(lv, 1070, 520, 400, 80, COL_NEON_ORANGE, false);
            AddPlatform(lv, 1150, 380, 180, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 1300, 260, 150, 20, COL_NEON_BLUE, false);

            // Hazard pit 3
            AddPlatform(lv, 1470, 570, 130, 30, COL_NEON_PINK, true);

            // Section 4: Reactor core area - wide open with catwalks
            AddPlatform(lv, 1600, 520, 700, 80, COL_NEON_ORANGE, false);
            AddPlatform(lv, 1700, 380, 200, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 1900, 250, 180, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 2100, 380, 150, 20, COL_NEON_BLUE, false);

            // Hazard pit 4
            AddPlatform(lv, 2300, 570, 100, 30, COL_NEON_PINK, true);

            // Section 5: Descending to lower levels
            AddPlatform(lv, 2400, 520, 500, 80, COL_NEON_ORANGE, false);
            AddPlatform(lv, 2500, 380, 160, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 2700, 260, 140, 20, COL_NEON_BLUE, false);

            // Hazard pit 5
            AddPlatform(lv, 2900, 570, 120, 30, COL_NEON_PINK, true);

            // Section 6: Final push
            AddPlatform(lv, 3020, 520, 500, 80, COL_NEON_ORANGE, false);
            AddPlatform(lv, 3150, 380, 180, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 3400, 260, 200, 20, COL_NEON_BLUE, false);

            // Hazard pit 6
            AddPlatform(lv, 3520, 570, 100, 30, COL_NEON_PINK, true);

            // Exit stretch
            AddPlatform(lv, 3620, 520, 580, 80, COL_NEON_ORANGE, false);
            AddPlatform(lv, 3800, 360, 160, 20, COL_NEON_BLUE, false);

            // Capsules
            SpawnCapsule(g, 700, 100, WEAPON_FLAME);
            SpawnCapsule(g, 1800, 100, WEAPON_SPREAD);
            SpawnCapsule(g, 2700, 100, WEAPON_LASER);

            // Enemies - heavy resistance
            SpawnEnemy(g, ENEMY_SNIPER, 300, 252);   // on platform y=300 (x:250-370)
            SpawnEnemy(g, ENEMY_TURRET, 550, 318);   // on platform y=350 (x:550-730)
            SpawnEnemy(g, ENEMY_RUNNER, 600, 450);
            SpawnEnemy(g, ENEMY_RUNNER, 800, 450);

            SpawnEnemy(g, ENEMY_TURRET, 1200, 348);  // on platform y=380 (x:1150-1330)
            SpawnEnemy(g, ENEMY_SNIPER, 1350, 212);  // on platform y=260 (x:1300-1450)
            SpawnEnemy(g, ENEMY_RUNNER, 1400, 450);

            SpawnEnemy(g, ENEMY_TURRET, 1750, 348);  // on platform y=380 (x:1700-1900)
            SpawnEnemy(g, ENEMY_RUNNER, 1850, 470);
            SpawnEnemy(g, ENEMY_SNIPER, 1950, 202);  // on platform y=250 (x:1900-2080)
            SpawnEnemy(g, ENEMY_RUNNER, 2100, 470);

            SpawnEnemy(g, ENEMY_SNIPER, 2550, 332);  // on platform y=380 (x:2500-2660)
            SpawnEnemy(g, ENEMY_TURRET, 2650, 348);  // on platform y=380 (x:2500-2660)
            SpawnEnemy(g, ENEMY_RUNNER, 2800, 470);

            SpawnEnemy(g, ENEMY_TURRET, 3200, 348);  // on platform y=380 (x:3150-3330)
            SpawnEnemy(g, ENEMY_SNIPER, 3450, 212);  // on platform y=260 (x:3400-3600)
            SpawnEnemy(g, ENEMY_RUNNER, 3700, 470);
            SpawnEnemy(g, ENEMY_RUNNER, 3900, 470);
            break;
        }

        case 2: {
            // STAGE 3: THE HEART OF CYBER-CORE (Boss Stage)
            // Linear approach leading to a wide boss arena with tactical platforms
            strcpy(lv->name, "STAGE 3: THE HEART OF CYBER-CORE");
            lv->length = 2200.0f;
            lv->spawn_point = (Vector2){ 80, 450 };
            lv->boss_trigger_point = (Vector2){ 1050.0f, 0.0f };
            lv->bg_color = (Color){ 24, 10, 18, 255 };
            lv->platform_color = COL_NEON_PINK;

            // Approach corridor (0 to 1050)
            AddPlatform(lv, 0, 520, 1050, 80, COL_NEON_PINK, false);
            AddPlatform(lv, 250, 380, 160, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 550, 280, 180, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 800, 380, 150, 20, COL_NEON_BLUE, false);

            // Boss arena floor split into segments with hazard gaps
            AddPlatform(lv, 1050, 520, 250, 80, COL_NEON_PINK, false);  // 1050-1300
            AddPlatform(lv, 1380, 520, 370, 80, COL_NEON_PINK, false);  // 1380-1750
            AddPlatform(lv, 1830, 520, 370, 80, COL_NEON_PINK, false);  // 1830-2200

            // Boss arena tactical platforms (covers and height advantage)
            AddPlatform(lv, 1100, 380, 200, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 1450, 280, 220, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 1600, 380, 200, 20, COL_NEON_BLUE, false);

            // Hazard pits in arena (actual gaps in the floor)
            AddPlatform(lv, 1300, 560, 80, 40, COL_NEON_PINK, true);
            AddPlatform(lv, 1750, 560, 80, 40, COL_NEON_PINK, true);

            // Pre-boss capsules  
            SpawnCapsule(g, 400, 150, WEAPON_SPREAD);
            SpawnCapsule(g, 800, 150, WEAPON_LASER);
            SpawnCapsule(g, 1200, 150, WEAPON_FLAME);

            // Approach enemies
            SpawnEnemy(g, ENEMY_SNIPER, 350, 472);   // on ground y=520
            SpawnEnemy(g, ENEMY_TURRET, 600, 248);   // on platform y=280 (x:550-730)
            SpawnEnemy(g, ENEMY_SNIPER, 850, 332);   // on platform y=380 (x:800-950)
            SpawnEnemy(g, ENEMY_RUNNER, 900, 470);
            break;
        }
    }

    // Scale enemy health with level difficulty
    float health_scale = 1.0f + (float)level_idx * 0.5f;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (g->enemies[i].active) {
            g->enemies[i].health = (int)(g->enemies[i].health * health_scale);
            g->enemies[i].max_health = g->enemies[i].health;
        }
    }

    g->player.pos = lv->spawn_point;
    g->player.vel = (Vector2){ 0, 0 };
    g->player.on_ground = false;
    g->player.invuln_timer = 2.0f; // Brief spawn shield
    g->player.jump_rotation = 0.0f;
    g->player.squash_x = 1.0f;
    g->player.squash_y = 1.0f;
    g->player.dash_ghost_count = 0;
    g->player.dash_ghost_timer = 0.0f;
    g->player.last_safe_ground_pos = lv->spawn_point;

    g->camera.target = g->player.pos;
    g->camera.offset = (Vector2){ SCREEN_W / 3.0f, SCREEN_H / 1.5f };
    g->camera.rotation = 0.0f;
    g->camera.zoom = 1.0f;
}

static void InitGame(GameState *g) {
    memset(g, 0, sizeof(GameState));

    g->player.max_shield = 100.0f;
    g->player.shield     = 100.0f;
    g->player.lives      = 3;
    g->player.weapon     = WEAPON_RIFLE;
    g->player.size       = (Vector2){ 32, 48 };
    g->player.alive      = true;
    
    // Juice
    g->player.squash_x = 1.0f;
    g->player.squash_y = 1.0f;

    g->screen = SCREEN_MENU;
    g->camera_shake = 0.0f;
    g->kill_count = 0;

    LoadLevel(g, 0);
}

/* ─────────────────────────── Particle System ─────────────────────────── */

static void AddParticleEx(GameState *g, Vector2 pos, Color col, float speed, float size, ParticleType type, float custom_life, int count) {
    for (int i = 0; i < count; i++) {
        if (g->particle_count >= MAX_PARTICLES) return;
        Particle *p = &g->particles[g->particle_count++];
        float angle = (float)GetRandomValue(0, 360) * DEG2RAD;
        float vel_len = speed * (0.4f + 0.6f * (float)GetRandomValue(0, 100) / 100.0f);
        p->pos = pos;
        p->vel = (Vector2){ cosf(angle) * vel_len, sinf(angle) * vel_len };
        p->color = col;
        p->max_life = (custom_life > 0.0f ? custom_life : (0.3f + 0.4f * (float)GetRandomValue(0, 100) / 100.0f));
        p->life = p->max_life;
        p->size = size * (0.6f + 0.5f * (float)GetRandomValue(0, 100) / 100.0f);
        p->type = type;
    }
}

static void AddParticle(GameState *g, Vector2 pos, Color col, float speed, float size, int count) {
    AddParticleEx(g, pos, col, speed, size, PARTICLE_SPARK, 0.0f, count);
}

static void SpawnCasing(GameState *g, Vector2 pos, bool facing_right) {
    if (g->particle_count >= MAX_PARTICLES) return;
    Particle *p = &g->particles[g->particle_count++];
    p->pos = pos;
    float dir = facing_right ? -1.0f : 1.0f;
    // fly off backward and up
    p->vel = (Vector2){ dir * (60.0f + GetRandomValue(0, 60)), -150.0f - GetRandomValue(0, 80) };
    p->color = (Color){ 230, 180, 40, 255 }; // gold
    p->max_life = 0.8f + (float)GetRandomValue(0, 40) / 100.0f;
    p->life = p->max_life;
    p->size = 3.0f;
    p->type = PARTICLE_CASING;
}

static void SpawnDust(GameState *g, Vector2 pos, int count) {
    for (int i = 0; i < count; i++) {
        if (g->particle_count >= MAX_PARTICLES) return;
        Particle *p = &g->particles[g->particle_count++];
        p->pos = pos;
        p->vel = (Vector2){ (float)GetRandomValue(-60, 60), (float)GetRandomValue(-15, -2) };
        p->color = (Color){ 160, 160, 180, 120 };
        p->max_life = 0.3f + (float)GetRandomValue(0, 25) / 100.0f;
        p->life = p->max_life;
        p->size = 3.0f + GetRandomValue(0, 4);
        p->type = PARTICLE_DUST;
    }
}

static void UpdateParticles(GameState *g, float dt) {
    Level *lv = &g->levels[g->current_level];
    for (int i = 0; i < g->particle_count; ) {
        Particle *p = &g->particles[i];
        p->life -= dt;
        p->pos.x += p->vel.x * dt;
        p->pos.y += p->vel.y * dt;

        if (p->type == PARTICLE_CASING) {
            p->vel.y += 1200.0f * dt; // gravity
            // simple collision with platforms
            for (int k = 0; k < lv->platform_count; k++) {
                Rectangle r = lv->platforms[k].rect;
                if (!lv->platforms[k].is_hazard && 
                    p->pos.x >= r.x && p->pos.x <= r.x + r.width &&
                    p->pos.y >= r.y && p->pos.y <= r.y + 8.0f && p->vel.y > 0) {
                    p->pos.y = r.y;
                    p->vel.y = -p->vel.y * 0.4f; // bounce
                    p->vel.x *= 0.6f;
                    break;
                }
            }
        } else if (p->type == PARTICLE_FIRE) {
            p->vel.y -= 120.0f * dt; // float up
            p->vel.x *= 0.94f; // friction
            p->size += dt * 10.0f; // expand
            
            float pct = p->life / p->max_life;
            if (pct > 0.6f) {
                p->color = (Color){ 255, (unsigned char)(255 * (pct - 0.6f) / 0.4f), 40, 255 };
            } else if (pct > 0.2f) {
                p->color = (Color){ 255, (unsigned char)(140 * (pct - 0.2f) / 0.4f), 20, 255 };
            } else {
                p->color = (Color){ 80, 80, 80, (unsigned char)(255 * pct / 0.2f) };
            }
        } else if (p->type == PARTICLE_DUST) {
            p->vel.y *= 0.9f;
            p->vel.x *= 0.9f;
            p->size += dt * 5.0f;
            p->color.a = (unsigned char)(120 * (p->life / p->max_life));
        } else {
            p->vel.y += 150.0f * dt; // standard gravity spark
        }

        if (p->life <= 0) {
            g->particles[i] = g->particles[--g->particle_count];
        } else {
            i++;
        }
    }
}

/* ─────────────────────────── Spawners ─────────────────────────── */

static void SpawnEnemy(GameState *g, EnemyType type, float x, float y) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!g->enemies[i].active) {
            Enemy *e = &g->enemies[i];
            memset(e, 0, sizeof(Enemy));
            e->type = type;
            e->pos = (Vector2){ x, y };
            e->vel = (Vector2){ 0, 0 };
            e->active = true;
            e->shoot_timer = (float)GetRandomValue(0, 100) / 100.0f * 1.5f;
            e->hit_flash = 0.0f;

            switch (type) {
                case ENEMY_RUNNER:
                    e->size = (Vector2){ 32, 48 };
                    e->health = 15.0f;
                    e->vel.x = -100.0f; // running left initially
                    break;
                case ENEMY_SNIPER:
                    e->size = (Vector2){ 32, 48 };
                    e->health = 25.0f;
                    break;
                case ENEMY_TURRET:
                    e->size = (Vector2){ 32, 32 };
                    e->health = 30.0f;
                    break;
                case ENEMY_BOSS_LEFT_TURRET:
                case ENEMY_BOSS_RIGHT_TURRET:
                    e->size = (Vector2){ 40, 40 };
                    e->health = 120.0f;
                    break;
                case ENEMY_BOSS_CORE:
                    e->size = (Vector2){ 64, 80 };
                    e->health = 300.0f;
                    e->boss_core_open = false;
                    break;
            }
            e->max_health = e->health;
            return;
        }
    }
}

static void SpawnCapsule(GameState *g, float x, float y, WeaponType drops) {
    for (int i = 0; i < MAX_CAPSULES; i++) {
        if (!g->capsules[i].active) {
            Capsule *c = &g->capsules[i];
            c->pos = (Vector2){ x, y };
            c->vel = (Vector2){ 80.0f, sinf(x) * 40.0f }; // sinewave float
            c->drops_weapon = drops;
            c->active = true;
            return;
        }
    }
}

/* ─────────────────────────── Collision Detection ─────────────────────────── */

static bool CheckRayCollisionRec2D(Vector2 start, Vector2 dir, Rectangle rect, float *t_collision) {
    float tmin = -999999.0f;
    float tmax = 999999.0f;

    if (dir.x != 0.0f) {
        float tx1 = (rect.x - start.x) / dir.x;
        float tx2 = (rect.x + rect.width - start.x) / dir.x;
        tmin = fmaxf(tmin, fminf(tx1, tx2));
        tmax = fminf(tmax, fmaxf(tx1, tx2));
    } else {
        if (start.x < rect.x || start.x > rect.x + rect.width) return false;
    }

    if (dir.y != 0.0f) {
        float ty1 = (rect.y - start.y) / dir.y;
        float ty2 = (rect.y + rect.height - start.y) / dir.y;
        tmin = fmaxf(tmin, fminf(ty1, ty2));
        tmax = fminf(tmax, fmaxf(ty1, ty2));
    } else {
        if (start.y < rect.y || start.y > rect.y + rect.height) return false;
    }

    if (tmax >= tmin && tmax >= 0.0f) {
        *t_collision = tmin >= 0.0f ? tmin : tmax;
        return true;
    }
    return false;
}

static bool IsEnemyInAimLine(GameState *g, Vector2 spawn_pos, Vector2 aim) {
    Level *lv = &g->levels[g->current_level];
    float closest_enemy_t = 999999.0f;
    bool hit_enemy = false;

    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->active) continue;

        Rectangle rect = { e->pos.x, e->pos.y, e->size.x, e->size.y };
        float t_collision;
        if (CheckRayCollisionRec2D(spawn_pos, aim, rect, &t_collision)) {
            if (t_collision >= 0.0f && t_collision < closest_enemy_t && t_collision < 900.0f) {
                closest_enemy_t = t_collision;
                hit_enemy = true;
            }
        }
    }

    if (!hit_enemy) return false;

    // Check if any wall blocks the path to the closest enemy
    for (int i = 0; i < lv->platform_count; i++) {
        Platform p = lv->platforms[i];
        if (p.is_hazard) continue;

        float t_wall;
        if (CheckRayCollisionRec2D(spawn_pos, aim, p.rect, &t_wall)) {
            if (t_wall >= 0.0f && t_wall < closest_enemy_t) {
                return false; // wall blocks!
            }
        }
    }

    return true;
}

static bool CheckCollisionRecs2(Vector2 pos1, Vector2 size1, Vector2 pos2, Vector2 size2) {
    return (pos1.x < pos2.x + size2.x && pos1.x + size1.x > pos2.x &&
            pos1.y < pos2.y + size2.y && pos1.y + size1.y > pos2.y);
}

// Split-axis specific check for X-axis
static bool CheckTileCollisionX(Level *lv, Vector2 pos, Vector2 size, float *push_x, bool *hazard_hit) {
    *push_x = 0;
    bool collided = false;

    for (int i = 0; i < lv->platform_count; i++) {
        Platform p = lv->platforms[i];
        Rectangle pr = p.rect;

        if (pos.x < pr.x + pr.width && pos.x + size.x > pr.x &&
            pos.y < pr.y + pr.height && pos.y + size.y > pr.y) {
            
            if (p.is_hazard) {
                *hazard_hit = true;
                continue;
            }

            collided = true;
            float overlap_x;
            float dir_x;
            if (pos.x + size.x/2.0f < pr.x + pr.width/2.0f) {
                overlap_x = (pos.x + size.x) - pr.x;
                dir_x = -overlap_x;
            } else {
                overlap_x = (pr.x + pr.width) - pos.x;
                dir_x = overlap_x;
            }
            
            if (fabsf(dir_x) > fabsf(*push_x)) {
                *push_x = dir_x;
            }
        }
    }
    return collided;
}

// Split-axis specific check for Y-axis
static bool CheckTileCollisionY(Level *lv, Vector2 pos, Vector2 size, float *push_y, bool *hazard_hit) {
    *push_y = 0;
    bool collided = false;

    for (int i = 0; i < lv->platform_count; i++) {
        Platform p = lv->platforms[i];
        Rectangle pr = p.rect;

        if (pos.x < pr.x + pr.width && pos.x + size.x > pr.x &&
            pos.y < pr.y + pr.height && pos.y + size.y > pr.y) {
            
            if (p.is_hazard) {
                *hazard_hit = true;
                continue;
            }

            collided = true;
            float overlap_y;
            float dir_y;
            if (pos.y + size.y/2.0f < pr.y + pr.height/2.0f) {
                overlap_y = (pos.y + size.y) - pr.y;
                dir_y = -overlap_y;
            } else {
                overlap_y = (pr.y + pr.height) - pos.y;
                dir_y = overlap_y;
            }
            
            if (fabsf(dir_y) > fabsf(*push_y)) {
                *push_y = dir_y;
            }
        }
    }
    return collided;
}

// Legacy standard check for backward compatibility (used by enemies)
static bool CheckTileCollision(Level *lv, Vector2 pos, Vector2 size, float *push_x, float *push_y, bool *hazard_hit) {
    *push_x = 0;
    *push_y = 0;
    bool collided = false;

    for (int i = 0; i < lv->platform_count; i++) {
        Platform p = lv->platforms[i];
        Rectangle pr = p.rect;

        if (pos.x < pr.x + pr.width && pos.x + size.x > pr.x &&
            pos.y < pr.y + pr.height && pos.y + size.y > pr.y) {
            
            if (p.is_hazard) {
                *hazard_hit = true;
                continue;
            }

            collided = true;
            float overlap_x = fminf(pos.x + size.x - pr.x, pr.x + pr.width - pos.x);
            float overlap_y = fminf(pos.y + size.y - pr.y, pr.y + pr.height - pos.y);

            if (overlap_x < overlap_y) {
                float dir_x = (pos.x + size.x/2 < pr.x + pr.width/2) ? -overlap_x : overlap_x;
                if (fabsf(dir_x) > fabsf(*push_x)) *push_x = dir_x;
            } else {
                float dir_y = (pos.y + size.y/2 < pr.y + pr.height/2) ? -overlap_y : overlap_y;
                if (fabsf(dir_y) > fabsf(*push_y)) *push_y = dir_y;
            }
        }
    }
    return collided;
}

/* ─────────────────────────── Weapon System ─────────────────────────── */

static void FirePlayerWeapon(GameState *g, Vector2 aim) {
    Player *p = &g->player;

    int dmg = 10;
    float rate = 0.2f;
    float spd = 800.0f;
    Color bullet_col = COL_NEON_BLUE;

    switch (p->weapon) {
        case WEAPON_RIFLE:
            rate = 0.16f;
            dmg = 12;
            spd = 900.0f;
            bullet_col = COL_NEON_BLUE;
            break;
        case WEAPON_SPREAD:
            rate = 0.25f;
            dmg = 10;
            spd = 750.0f;
            bullet_col = COL_NEON_PINK;
            break;
        case WEAPON_LASER:
            rate = 0.07f;
            dmg = 8;
            spd = 1400.0f;
            bullet_col = COL_NEON_GREEN;
            break;
        case WEAPON_FLAME:
            rate = 0.25f;
            dmg = 18;
            spd = 500.0f;
            bullet_col = COL_NEON_ORANGE;
            break;
    }

    if (p->shoot_cooldown > 0) return;
    p->shoot_cooldown = rate;

    bool is_crouching = p->on_ground && (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S));
    Vector2 spawn_pos = (Vector2){ p->pos.x + p->size.x/2.0f + aim.x * 20.0f, p->pos.y + (is_crouching ? p->size.y * 0.7f : p->size.y/2.5f) + aim.y * 20.0f };

    // Play SFX & Spawn Casings
    switch (p->weapon) {
        case WEAPON_RIFLE:
            PlaySynthSFX(800.0f, 150.0f, 0.12f, 0.35f, false);
            SpawnCasing(g, spawn_pos, p->facing_right);
            g->camera_shake = 0.05f;
            break;
        case WEAPON_SPREAD:
            PlaySynthSFX(500.0f, 90.0f, 0.16f, 0.45f, false);
            for (int k = 0; k < 3; k++) SpawnCasing(g, spawn_pos, p->facing_right);
            g->camera_shake = 0.2f; // heavy kickback shake!
            break;
        case WEAPON_LASER:
            PlaySynthSFX(1300.0f, 500.0f, 0.08f, 0.28f, false);
            g->camera_shake = 0.03f;
            break;
        case WEAPON_FLAME:
            PlaySynthSFX(320.0f, 80.0f, 0.22f, 0.5f, true); // noisy crackle
            g->camera_shake = 0.08f;
            break;
    }

    if (p->weapon == WEAPON_SPREAD) {
        // Fire 5 bullets in a spreading fan
        float base_ang = atan2f(aim.y, aim.x);
        float spread_steps[5] = { -0.25f, -0.12f, 0.0f, 0.12f, 0.25f };
        for (int b = 0; b < 5; b++) {
            float ang = base_ang + spread_steps[b];
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!g->bullets[i].active) {
                    Bullet *bullet = &g->bullets[i];
                    bullet->pos = spawn_pos;
                    bullet->vel = (Vector2){ cosf(ang) * spd, sinf(ang) * spd };
                    bullet->type = WEAPON_SPREAD;
                    bullet->angle = ang;
                    bullet->damage = dmg;
                    bullet->active = true;
                    bullet->timer = 0;
                    break;
                }
            }
        }
    } else if (p->weapon == WEAPON_FLAME) {
        // Flame gun spawns fire bullets and visual expanding fire particles
        float base_ang = atan2f(aim.y, aim.x);
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!g->bullets[i].active) {
                Bullet *bullet = &g->bullets[i];
                bullet->pos = spawn_pos;
                bullet->vel = (Vector2){ cosf(base_ang) * spd, sinf(base_ang) * spd };
                bullet->type = WEAPON_FLAME;
                bullet->angle = base_ang;
                bullet->damage = dmg;
                bullet->active = true;
                bullet->timer = 0;
                break;
            }
        }
        // Spawn fire smoke/flame particles in firing direction
        for (int fp = 0; fp < 3; fp++) {
            if (g->particle_count < MAX_PARTICLES) {
                Particle *part = &g->particles[g->particle_count++];
                part->pos = spawn_pos;
                float dev = ((float)GetRandomValue(-15, 15) * DEG2RAD);
                float fspd = spd * (0.3f + 0.4f * (float)GetRandomValue(0, 100) / 100.0f);
                part->vel = (Vector2){ cosf(base_ang + dev) * fspd, sinf(base_ang + dev) * fspd };
                part->color = (Color){ 255, 200, 50, 255 };
                part->max_life = 0.22f + (float)GetRandomValue(0, 12) / 100.0f;
                part->life = part->max_life;
                part->size = 5.0f + GetRandomValue(0, 6);
                part->type = PARTICLE_FIRE;
            }
        }
    } else {
        // Normal Rifle or Laser
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!g->bullets[i].active) {
                Bullet *bullet = &g->bullets[i];
                bullet->pos = spawn_pos;
                bullet->vel = (Vector2){ aim.x * spd, aim.y * spd };
                bullet->type = p->weapon;
                bullet->angle = atan2f(aim.y, aim.x);
                bullet->damage = dmg;
                bullet->active = true;
                bullet->timer = 0;
                break;
            }
        }
    }

    // Small shooting flash particles
    AddParticle(g, spawn_pos, bullet_col, 100.0f, 3.0f, 4);
}

/* ─────────────────────────── Damage / Death ─────────────────────────── */

static void DamagePlayer(GameState *g, float dmg) {
    Player *p = &g->player;
    if (p->invuln_timer > 0 || !p->alive) return;

    p->shield -= dmg;
    g->camera_shake = 0.35f;
    
    // Play Hurt Sound
    PlaySynthSFX(180.0f, 60.0f, 0.22f, 0.6f, true); // heavy noisy impact grunt
    
    AddParticleEx(g, (Vector2){ p->pos.x + p->size.x/2, p->pos.y + p->size.y/2 }, COL_NEON_PINK, 200.0f, 4.5f, PARTICLE_SPARK, 0.6f, 15);

    if (p->shield <= 0) {
        p->shield = 0;
        p->lives--;
        // Explosion sound
        PlaySynthSFX(120.0f, 30.0f, 0.6f, 0.8f, true);
        
        if (p->lives < 0) {
            p->alive = false;
            g->screen = SCREEN_GAMEOVER;
            AddParticleEx(g, (Vector2){ p->pos.x + p->size.x/2, p->pos.y + p->size.y/2 }, COL_NEON_PINK, 280.0f, 6.0f, PARTICLE_SPARK, 1.0f, 50);
        } else {
            // Respawn trigger shield
            p->shield = p->max_shield;
            p->invuln_timer = 2.0f;
            // Respawn at boss arena entrance if boss is active, otherwise at level start
            if (g->boss_active) {
                Vector2 boss_respawn = { g->levels[g->current_level].boss_trigger_point.x - 50.0f, 450.0f };
                p->pos = boss_respawn;
                p->last_safe_ground_pos = boss_respawn;
            } else {
                p->pos = g->levels[g->current_level].spawn_point;
                p->last_safe_ground_pos = g->levels[g->current_level].spawn_point;
            }
            p->vel = (Vector2){ 0, 0 };
            p->jump_rotation = 0;
            p->squash_x = 1.0f;
            p->squash_y = 1.0f;
        }
    }
}

/* ─────────────────────────── Update Game ─────────────────────────── */

static void UpdateGame(GameState *g, float dt) {
    Level *lv = &g->levels[g->current_level];
    Player *p = &g->player;

    if (g->screen != SCREEN_PLAY) return;

    // Debug: F1 skips to boss level
    if (IsKeyPressed(KEY_F1)) {
        g->kill_count = 0;
        g->player.shield = g->player.max_shield;
        g->player.lives = 9;
        g->player.weapon = WEAPON_SPREAD;
        LoadLevel(g, 2);
        return;
    }

    // Decay timers
    if (p->invuln_timer > 0) p->invuln_timer -= dt;
    if (p->shoot_cooldown > 0) p->shoot_cooldown -= dt;
    if (p->dash_cooldown > 0) p->dash_cooldown -= dt;
    if (g->camera_shake > 0) g->camera_shake -= dt * 3.0f;

    // Decay squash and stretch back to 1
    p->squash_x += (1.0f - p->squash_x) * 12.0f * dt;
    p->squash_y += (1.0f - p->squash_y) * 12.0f * dt;

    /* ── Camera Lock and Boss Trigger ── */
    if (lv->boss_trigger_point.x > 0 && p->pos.x >= lv->boss_trigger_point.x) {
        if (!g->boss_active) {
            g->boss_active = true;
            g->boss_intro_timer = 2.0f;
            // Spawn Boss segments at fixed arena position (visible in camera lock)
            float boss_x = lv->boss_trigger_point.x + 600.0f;
            SpawnEnemy(g, ENEMY_BOSS_LEFT_TURRET, boss_x, 200);
            SpawnEnemy(g, ENEMY_BOSS_RIGHT_TURRET, boss_x, 420);
            SpawnEnemy(g, ENEMY_BOSS_CORE, boss_x + 6, 290);
        }
    }

    /* ── Player Movement Input ── */
    float move_x = 0;
    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) {
        move_x -= 1.0f;
        p->facing_right = false;
    }
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) {
        move_x += 1.0f;
        p->facing_right = true;
    }

    // Dash control
    if (p->dash_timer > 0) {
        p->dash_timer -= dt;
        p->vel.x = p->dash_dir.x * MOVE_SPEED * 2.2f;
        p->vel.y = p->dash_dir.y * MOVE_SPEED * 2.2f;

        // Spawn dash ghosts
        p->dash_ghost_timer += dt;
        if (p->dash_ghost_timer >= 0.03f) {
            p->dash_ghost_timer = 0;
            for (int k = 2; k > 0; k--) {
                p->dash_ghosts[k] = p->dash_ghosts[k-1];
            }
            p->dash_ghosts[0] = p->pos;
            if (p->dash_ghost_count < 3) p->dash_ghost_count++;
        }
    } else {
        bool is_crouching = p->on_ground && (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S));
        if (is_crouching) {
            p->vel.x = 0;
        } else {
            p->vel.x = move_x * MOVE_SPEED;
        }

        // Apply Gravity
        p->vel.y += GRAVITY * dt;
        if (p->vel.y > 800.0f) p->vel.y = 800.0f; // Terminal velocity
        p->dash_ghost_count = 0;
    }

    // Dash trigger
    if (IsKeyPressed(KEY_K) || IsKeyPressed(KEY_Z)) {
        if (p->dash_cooldown <= 0) {
            float dx = move_x;
            float dy = 0;
            if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) dy -= 1.0f;
            if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) dy += 1.0f;

            if (dx == 0 && dy == 0) {
                dx = p->facing_right ? 1.0f : -1.0f;
            }

            // Normalise dash dir
            float len = sqrtf(dx*dx + dy*dy);
            p->dash_dir = (Vector2){ dx/len, dy/len };
            p->dash_timer = 0.16f;
            p->dash_cooldown = 0.8f;
            p->dash_ghost_count = 0;
            p->dash_ghost_timer = 0;

            // Dash whoosh SFX
            PlaySynthSFX(140.0f, 440.0f, 0.18f, 0.45f, true);

            AddParticle(g, (Vector2){ p->pos.x + p->size.x/2, p->pos.y + p->size.y/2 }, COL_NEON_BLUE, 100.0f, 3.0f, 10);
        }
    }

    // Somersault spin updating
    if (!p->on_ground) {
        if (p->jump_rotation != 0) {
            p->jump_rotation += (p->facing_right ? 720.0f : -720.0f) * dt;
        }
    } else {
        if (p->jump_rotation != 0) {
            p->jump_rotation = 0;
            p->squash_y = 0.65f; // land squash!
            p->squash_x = 1.35f;
            PlaySynthSFX(140.0f, 90.0f, 0.1f, 0.35f, true); // Land sound!
            SpawnDust(g, (Vector2){ p->pos.x + p->size.x/2, p->pos.y + p->size.y }, 8);
        }
    }

    // Jump
    if (IsKeyPressed(KEY_SPACE) && p->on_ground) {
        p->vel.y = JUMP_FORCE;
        p->on_ground = false;
        p->jump_rotation = 0.1f; // start spin somersault
        p->squash_y = 1.35f; // jump stretch!
        p->squash_x = 0.65f;
        PlaySynthSFX(220.0f, 880.0f, 0.15f, 0.3f, false); // Jump tone sweep
        SpawnDust(g, (Vector2){ p->pos.x + p->size.x/2, p->pos.y + p->size.y }, 6);
    }

    /* ── Resolve Player Tile Collisions ── */
    bool hazard_hit = false;
    float px, py;

    // Move X (Split Axis Resolution)
    p->pos.x += p->vel.x * dt;
    // Boundary lock
    if (p->pos.x < 10) p->pos.x = 10;
    if (g->boss_active) {
        // Lock camera scrolling, player can't retreat left of arena boundary
        float lock_x = lv->boss_trigger_point.x;
        if (p->pos.x < lock_x) p->pos.x = lock_x;
    }
    if (p->pos.x + p->size.x > lv->length - 10) p->pos.x = lv->length - 10 - p->size.x;

    CheckTileCollisionX(lv, p->pos, p->size, &px, &hazard_hit);
    p->pos.x += px;

    // Move Y (Split Axis Resolution)
    p->pos.y += p->vel.y * dt;
    p->on_ground = false;
    
    CheckTileCollisionY(lv, p->pos, p->size, &py, &hazard_hit);
    p->pos.y += py;
    if (py != 0) {
        if (p->vel.y > 0 && py < 0) p->on_ground = true;
        p->vel.y = 0;
    }

    // Record last safe position if player is standing on safe ground
    if (p->on_ground && !hazard_hit) {
        p->last_safe_ground_pos = p->pos;
    }

    // If bottom boundary hit or hazard hit (respawn at last safe ground position!)
    if (p->pos.y + p->size.y > SCREEN_H + 40 || hazard_hit) {
        DamagePlayer(g, 40.0f);
        p->pos = p->last_safe_ground_pos;
        p->vel = (Vector2){0, 0};
    }

    /* ── Player Aim Direction ── */
    bool is_crouching = p->on_ground && (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S));

    Vector2 aim = { 1.0f, 0.0f };
    if (p->facing_right) aim.x = 1.0f;
    else aim.x = -1.0f;

    float adx = 0, ady = 0;
    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) adx -= 1.0f;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) adx += 1.0f;
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) ady -= 1.0f;
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) ady += 1.0f;

    if (is_crouching) {
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) {
            aim.y = -1.0f;
            if (adx != 0) aim.x = adx;
            else aim.x = 0;
        } else {
            aim.y = 0;
            if (adx != 0) aim.x = adx;
        }
    } else {
        if (ady < 0) { // aiming up
            aim.y = -1.0f;
            if (adx == 0) aim.x = 0; // straight up
        } else if (ady > 0 && !p->on_ground) { // aiming down in air
            aim.y = 1.0f;
            if (adx == 0) aim.x = 0; // straight down
        } else {
            aim.y = 0;
            if (adx != 0) aim.x = adx;
        }
    }
    // Normalize
    float aim_len = sqrtf(aim.x*aim.x + aim.y*aim.y);
    if (aim_len > 0) { aim.x /= aim_len; aim.y /= aim_len; }

    /* ── Player Shoot ── */
    Vector2 spawn_pos = (Vector2){ p->pos.x + p->size.x/2.0f + aim.x * 20.0f, p->pos.y + (is_crouching ? p->size.y * 0.7f : p->size.y/2.5f) + aim.y * 20.0f };
    bool auto_shoot = IsEnemyInAimLine(g, spawn_pos, aim);

    if (IsKeyDown(KEY_J) || IsKeyDown(KEY_X) || auto_shoot) {
        FirePlayerWeapon(g, aim);
    }

    /* ── Update Player Bullets ── */
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &g->bullets[i];
        if (!b->active) continue;

        b->timer += dt;
        
        if (b->type == WEAPON_FLAME) {
            // Corkscrew motion
            float forward_spd = 500.0f;
            float freq = 20.0f;
            float amp = 150.0f;
            b->pos.x += cosf(b->angle) * forward_spd * dt - sinf(b->angle) * cosf(b->timer * freq) * amp * dt;
            b->pos.y += sinf(b->angle) * forward_spd * dt + cosf(b->angle) * cosf(b->timer * freq) * amp * dt;
            
            // Spawn fire sparkles
            if (GetRandomValue(0, 2) == 0) {
                if (g->particle_count < MAX_PARTICLES) {
                    Particle *part = &g->particles[g->particle_count++];
                    part->pos = b->pos;
                    part->vel = (Vector2){ (float)GetRandomValue(-20, 20), (float)GetRandomValue(-20, 20) };
                    part->color = (Color){ 255, 120, 30, 255 };
                    part->max_life = 0.3f + (float)GetRandomValue(0, 20) / 100.0f;
                    part->life = part->max_life;
                    part->size = 4.0f + GetRandomValue(0, 4);
                    part->type = PARTICLE_FIRE;
                }
            }
        } else {
            // Direct movement
            b->pos.x += b->vel.x * dt;
            b->pos.y += b->vel.y * dt;
        }

        // Deactivate bullets off-screen or after timeout
        if (b->timer > 1.5f || b->pos.x < 0 || b->pos.x > lv->length || b->pos.y < 0 || b->pos.y > SCREEN_H) {
            b->active = false;
        }
    }

    /* ── Update Floating Capsules ── */
    for (int i = 0; i < MAX_CAPSULES; i++) {
        Capsule *c = &g->capsules[i];
        if (!c->active) continue;

        c->pos.x += c->vel.x * dt;
        c->pos.y = c->pos.y + sinf((float)GetTime() * 4.0f) * 1.5f; // wave floating

        // Particle trail
        if (GetRandomValue(0, 4) == 0) {
            AddParticle(g, c->pos, COL_NEON_BLUE, 20.0f, 1.5f, 1);
        }

        // Collision with player bullets
        for (int b = 0; b < MAX_BULLETS; b++) {
            Bullet *bul = &g->bullets[b];
            if (bul->active && CheckCollisionRecs2(bul->pos, (Vector2){6,6}, c->pos, (Vector2){40,24})) {
                bul->active = false;
                c->active = false;
                
                // Spawn weapon item drop directly
                AddParticle(g, c->pos, COL_NEON_BLUE, 200.0f, 4.0f, 25);
                
                // Play Powerup Sound
                PlaySynthSFX(440.0f, 1320.0f, 0.35f, 0.5f, false);
                
                p->weapon = c->drops_weapon;
                p->shield += 50.0f;
                if (p->shield > p->max_shield) p->shield = p->max_shield;
                g->camera_shake = 0.15f;
            }
        }

        if (c->pos.x > lv->length + 100) c->active = false;
    }

    /* ── Update Enemies ── */
    int active_boss_parts = 0;
    bool core_destroyed = false;

    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->active) continue;

        e->shoot_timer += dt;
        if (e->hit_flash > 0) e->hit_flash -= dt; // decrement hit flash

        if (e->type == ENEMY_BOSS_LEFT_TURRET || e->type == ENEMY_BOSS_RIGHT_TURRET || e->type == ENEMY_BOSS_CORE) {
            active_boss_parts++;
        }

        switch (e->type) {
            case ENEMY_RUNNER: {
                // Runs left or right towards the player
                float speed = 120.0f;
                e->vel.x = (p->pos.x < e->pos.x) ? -speed : speed;
                e->pos.x += e->vel.x * dt;
                e->facing_right = e->vel.x > 0;

                // Handle simple tile collision to snap on ground
                e->pos.y += GRAVITY * dt;
                float ex, ey;
                bool eh;
                CheckTileCollision(lv, e->pos, e->size, &ex, &ey, &eh);
                e->pos.x += ex;
                e->pos.y += ey;

                // Hit player
                bool is_crouching = p->on_ground && (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S));
                Vector2 p_size = is_crouching ? (Vector2){ p->size.x, p->size.y / 2.0f } : p->size;
                Vector2 p_pos = is_crouching ? (Vector2){ p->pos.x, p->pos.y + p->size.y / 2.0f } : p->pos;
                if (CheckCollisionRecs2(e->pos, e->size, p_pos, p_size)) {
                    DamagePlayer(g, 15.0f);
                }
                break;
            }
            case ENEMY_SNIPER: {
                // Standing sniper aims laser pointer at player
                Vector2 dir = { p->pos.x + p->size.x/2 - e->pos.x, p->pos.y + p->size.y/2 - e->pos.y };
                float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
                e->facing_right = dir.x > 0;

                if (len < 600.0f) {
                    if (e->shoot_timer >= 2.0f) {
                        e->shoot_timer = 0;
                        // Fire a fast sniper bullet
                        PlaySynthSFX(900.0f, 600.0f, 0.15f, 0.22f, false);
                        for (int eb = 0; eb < MAX_ENEMY_BULS; eb++) {
                            if (!g->enemy_bullets[eb].active) {
                                EnemyBullet *ebul = &g->enemy_bullets[eb];
                                ebul->pos = (Vector2){ e->pos.x + (e->facing_right ? 20 : -10), e->pos.y + 12 };
                                ebul->vel = (Vector2){ (dir.x/len) * 500.0f, (dir.y/len) * 500.0f };
                                ebul->active = true;
                                break;
                            }
                        }
                    }
                }
                break;
            }
            case ENEMY_TURRET: {
                // Rotating wall turret aims directly at player
                Vector2 dir = { p->pos.x + p->size.x/2 - e->pos.x, p->pos.y + p->size.y/2 - e->pos.y };
                float len = sqrtf(dir.x*dir.x + dir.y*dir.y);

                if (len < 500.0f) {
                    if (e->shoot_timer >= 1.6f) {
                        e->shoot_timer = 0;
                        PlaySynthSFX(600.0f, 300.0f, 0.15f, 0.22f, false);
                        for (int eb = 0; eb < MAX_ENEMY_BULS; eb++) {
                            if (!g->enemy_bullets[eb].active) {
                                EnemyBullet *ebul = &g->enemy_bullets[eb];
                                ebul->pos = e->pos;
                                ebul->vel = (Vector2){ (dir.x/len) * 350.0f, (dir.y/len) * 350.0f };
                                ebul->active = true;
                                break;
                            }
                        }
                    }
                }
                break;
            }
            case ENEMY_BOSS_LEFT_TURRET: {
                // Top turret of the giant Wall Boss
                Vector2 dir = { p->pos.x + p->size.x/2 - e->pos.x, p->pos.y + p->size.y/2 - e->pos.y };
                float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
                if (e->shoot_timer >= 2.5f) {
                    e->shoot_timer = 0;
                    PlaySynthSFX(500.0f, 200.0f, 0.22f, 0.35f, false);
                    // Spread shot of 3 bullets
                    float base_ang = atan2f(dir.y, dir.x);
                    float spread[3] = { -0.15f, 0.0f, 0.15f };
                    for (int s = 0; s < 3; s++) {
                        float ang = base_ang + spread[s];
                        for (int eb = 0; eb < MAX_ENEMY_BULS; eb++) {
                            if (!g->enemy_bullets[eb].active) {
                                EnemyBullet *ebul = &g->enemy_bullets[eb];
                                ebul->pos = e->pos;
                                ebul->vel = (Vector2){ cosf(ang) * 250.0f, sinf(ang) * 250.0f };
                                ebul->active = true;
                                break;
                            }
                        }
                    }
                }
                break;
            }
            case ENEMY_BOSS_RIGHT_TURRET: {
                // Bottom turret of the giant Wall Boss
                Vector2 dir = { p->pos.x + p->size.x/2 - e->pos.x, p->pos.y + p->size.y/2 - e->pos.y };
                float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
                if (e->shoot_timer >= 1.8f) {
                    e->shoot_timer = 0;
                    PlaySynthSFX(800.0f, 400.0f, 0.15f, 0.25f, false);
                    // Direct fire laser bullet
                    for (int eb = 0; eb < MAX_ENEMY_BULS; eb++) {
                        if (!g->enemy_bullets[eb].active) {
                            EnemyBullet *ebul = &g->enemy_bullets[eb];
                            ebul->pos = e->pos;
                            ebul->vel = (Vector2){ (dir.x/len) * 320.0f, (dir.y/len) * 320.0f };
                            ebul->active = true;
                            break;
                        }
                    }
                }
                break;
            }
            case ENEMY_BOSS_CORE: {
                // Center reactor core shield
                // Cycles between open (vulnerable) and closed (invulnerable)
                float cycle = fmodf((float)GetTime(), 4.0f);
                e->boss_core_open = (cycle > 2.0f);

                if (e->boss_core_open) {
                    if (e->shoot_timer >= 0.9f) {
                        e->shoot_timer = 0;
                        PlaySynthSFX(300.0f, 100.0f, 0.25f, 0.35f, true); // fireball crackle
                        // Spits fireballs straight left
                        for (int eb = 0; eb < MAX_ENEMY_BULS; eb++) {
                            if (!g->enemy_bullets[eb].active) {
                                EnemyBullet *ebul = &g->enemy_bullets[eb];
                                ebul->pos = (Vector2){ e->pos.x, e->pos.y + e->size.y/2 };
                                ebul->vel = (Vector2){ -220.0f, (float)GetRandomValue(-50, 50) };
                                ebul->active = true;
                                break;
                            }
                        }
                    }
                }
                break;
            }
        }

        // Collision with player bullets
        for (int b = 0; b < MAX_BULLETS; b++) {
            Bullet *bul = &g->bullets[b];
            if (bul->active && CheckCollisionRecs2(bul->pos, (Vector2){8,8}, e->pos, e->size)) {
                bul->active = false;

                // Invulnerability for boss core when closed
                if (e->type == ENEMY_BOSS_CORE && !e->boss_core_open) {
                    // Deflected
                    AddParticle(g, bul->pos, LIGHTGRAY, 100.0f, 2.0f, 3);
                    PlaySynthSFX(900.0f, 950.0f, 0.05f, 0.22f, false);
                    continue;
                }

                e->health -= bul->damage;
                e->hit_flash = 0.08f; // set hit flash timer
                
                // Play impact SFX
                PlaySynthSFX(300.0f, 150.0f, 0.08f, 0.35f, true);

                AddParticle(g, bul->pos, COL_NEON_ORANGE, 120.0f, 2.5f, 4);

                if (e->health <= 0) {
                    e->active = false;
                    g->kill_count++;
                    
                    // Explosion SFX
                    PlaySynthSFX(160.0f, 35.0f, 0.42f, 0.75f, true);

                    // Bigger explosion for boss parts
                    int boom_particles = (e->type == ENEMY_BOSS_CORE || e->type == ENEMY_BOSS_LEFT_TURRET || e->type == ENEMY_BOSS_RIGHT_TURRET) ? 50 : 25;
                    float boom_size = (e->type == ENEMY_BOSS_CORE) ? 8.0f : 5.0f;
                    AddParticleEx(g, (Vector2){ e->pos.x + e->size.x/2, e->pos.y + e->size.y/2 }, COL_NEON_ORANGE, 200.0f, boom_size, PARTICLE_SPARK, 0.6f, boom_particles);
                    g->camera_shake = (e->type == ENEMY_BOSS_CORE) ? 0.8f : 0.22f;

                    if (e->type == ENEMY_BOSS_CORE) {
                        core_destroyed = true;
                    }
                }
            }
        }
    }

    // Boss Core destroyed = Level Clear / Game Win!
    if (core_destroyed) {
        g->boss_active = false;
        if (g->current_level < LEVEL_COUNT - 1) {
            LoadLevel(g, g->current_level + 1);
        } else {
            g->screen = SCREEN_WIN;
        }
    }

    /* ── Update Enemy Bullets ── */
    for (int i = 0; i < MAX_ENEMY_BULS; i++) {
        EnemyBullet *eb = &g->enemy_bullets[i];
        if (!eb->active) continue;

        eb->pos.x += eb->vel.x * dt;
        eb->pos.y += eb->vel.y * dt;

        // Hit player
        bool is_crouching = p->on_ground && (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S));
        Vector2 p_size = is_crouching ? (Vector2){ p->size.x, p->size.y / 2.0f } : p->size;
        Vector2 p_pos = is_crouching ? (Vector2){ p->pos.x, p->pos.y + p->size.y / 2.0f } : p->pos;
        if (CheckCollisionRecs2(eb->pos, (Vector2){8,8}, p_pos, p_size)) {
            eb->active = false;
            DamagePlayer(g, 15.0f);
        }

        // Off-screen
        if (eb->pos.x < p->pos.x - SCREEN_W || eb->pos.x > p->pos.x + SCREEN_W ||
            eb->pos.y < 0 || eb->pos.y > SCREEN_H) {
            eb->active = false;
        }
    }

    /* ── Camera Tracking ── */
    float target_x = p->pos.x;
    if (g->boss_active) {
        // Follow player but don't scroll left past boss arena entrance
        float arena_center = lv->boss_trigger_point.x + SCREEN_W / 3.0f;
        target_x = fmaxf(p->pos.x, arena_center);
    }

    g->camera.target.x = target_x;
    g->camera.target.y = 300.0f; // locked vertical tracking

    // Clamp camera within level bounds
    if (g->camera.target.x < SCREEN_W / 3.0f) {
        g->camera.target.x = SCREEN_W / 3.0f;
    }
    if (g->camera.target.x > lv->length - SCREEN_W * 2.0f / 3.0f) {
        g->camera.target.x = lv->length - SCREEN_W * 2.0f / 3.0f;
    }

    // Apply camera shake
    if (g->camera_shake > 0) {
        g->camera.offset.x = (SCREEN_W / 3.0f) + (float)GetRandomValue(-100, 100) / 100.0f * g->camera_shake * 40.0f;
        g->camera.offset.y = (SCREEN_H / 1.5f) + (float)GetRandomValue(-100, 100) / 100.0f * g->camera_shake * 40.0f;
    } else {
        g->camera.offset = (Vector2){ SCREEN_W / 3.0f, SCREEN_H / 1.5f };
    }

    /* ── Particle update ── */
    UpdateParticles(g, dt);

    /* ── Level Exit (Level 1 & 2 only) ── */
    if (!g->boss_active && p->pos.x >= lv->length - 80) {
        if (g->current_level < LEVEL_COUNT - 1) {
            LoadLevel(g, g->current_level + 1);
        } else {
            g->screen = SCREEN_WIN;
        }
    }
}

/* ─────────────────────────── Rendering ─────────────────────────── */

static void DrawParticles(GameState *g) {
    for (int i = 0; i < g->particle_count; i++) {
        Particle *p = &g->particles[i];
        float alpha = p->life / p->max_life;
        Color c = p->color;
        c.a = (unsigned char)(alpha * p->color.a);
        DrawRectangle((int)p->pos.x, (int)p->pos.y, (int)p->size, (int)p->size, c);
    }
}

static void DrawGame(GameState *g) {
    Level *lv = &g->levels[g->current_level];
    Player *p = &g->player;
    float t = (float)GetTime();

    if (g->screen == SCREEN_MENU) {
        ClearBackground(COL_SKY);

        // Title text glow
        float glow = sinf(t * 5.0f) * 4.0f;
        DrawText("CYBER COMMANDO", SCREEN_W/2 - MeasureText("CYBER COMMANDO", 48)/2 + (int)glow/2, 120, 48, COL_NEON_PINK);
        DrawText("CYBER COMMANDO", SCREEN_W/2 - MeasureText("CYBER COMMANDO", 48)/2, 120, 48, COL_NEON_BLUE);

        DrawText("A NEON RETRO RUN-AND-GUN SHOOTER", SCREEN_W/2 - MeasureText("A NEON RETRO RUN-AND-GUN SHOOTER", 16)/2, 180, 16, COL_CYBER_PURPLE);

        // Menu buttons
        DrawRectangle(SCREEN_W/2 - 120, 260, 240, 50, (Color){ 30, 20, 50, 250 });
        DrawRectangleLines(SCREEN_W/2 - 120, 260, 240, 50, COL_NEON_GREEN);
        DrawText("PRESS ENTER TO PLAY", SCREEN_W/2 - MeasureText("PRESS ENTER TO PLAY", 16)/2, 276, 16, COL_NEON_GREEN);

        // Controls box
        DrawRectangle(SCREEN_W/2 - 250, 360, 500, 180, (Color){ 20, 15, 30, 200 });
        DrawRectangleLines(SCREEN_W/2 - 250, 360, 500, 180, COL_CYBER_PURPLE);
        
        DrawText("CONTROLS:", SCREEN_W/2 - 230, 375, 14, COL_NEON_BLUE);
        DrawText("- Move: A/D or Left/Right Keys", SCREEN_W/2 - 230, 400, 14, WHITE);
        DrawText("- Aim: W/S or Up/Down Keys (Down in mid-air only)", SCREEN_W/2 - 230, 422, 14, WHITE);
        DrawText("- Jump: SPACE BAR", SCREEN_W/2 - 230, 444, 14, WHITE);
        DrawText("- Shoot: J or X Key (Hold to Autofire)", SCREEN_W/2 - 230, 466, 14, WHITE);
        DrawText("- Dash: K or Z Key (Directional)", SCREEN_W/2 - 230, 488, 14, WHITE);
        DrawText("- Quit / Back: ESC Key", SCREEN_W/2 - 230, 510, 14, WHITE);

        if (IsKeyPressed(KEY_ENTER)) {
            g->screen = SCREEN_PLAY;
            LoadLevel(g, 0);
        }
        return;
    }

    if (g->screen == SCREEN_GAMEOVER) {
        ClearBackground((Color){ 15, 5, 5, 255 });

        float shake = sinf(t * 15.0f) * 3.0f;
        DrawText("MISSION FAILED", SCREEN_W/2 - MeasureText("MISSION FAILED", 48)/2 + (int)shake, SCREEN_H/2 - 80, 48, COL_NEON_PINK);
        
        char buf[128];
        snprintf(buf, sizeof(buf), "You reached %s", lv->name);
        DrawText(buf, SCREEN_W/2 - MeasureText(buf, 20)/2, SCREEN_H/2 - 20, 20, LIGHTGRAY);

        DrawText("PRESS R TO RESTART OR ESC FOR MENU", SCREEN_W/2 - MeasureText("PRESS R TO RESTART OR ESC FOR MENU", 16)/2, SCREEN_H/2 + 40, 16, COL_NEON_BLUE);

        if (IsKeyPressed(KEY_R)) {
            InitGame(g);
            g->screen = SCREEN_PLAY;
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            g->screen = SCREEN_MENU;
        }
        return;
    }

    if (g->screen == SCREEN_WIN) {
        ClearBackground((Color){ 5, 15, 10, 255 });
        
        // Spawn win screen background celebrations
        for (int i = 0; i < 2; i++) {
            AddParticle(g, (Vector2){ (float)GetRandomValue(0, SCREEN_W), (float)GetRandomValue(0, SCREEN_H/2) },
                        (Color){(unsigned char)GetRandomValue(100, 255), (unsigned char)GetRandomValue(100, 255), 255, 255},
                        50.0f, 3.0f, 1);
        }
        UpdateParticles(g, GetFrameTime());
        DrawParticles(g);

        DrawText("MISSION ACCOMPLISHED!", SCREEN_W/2 - MeasureText("MISSION ACCOMPLISHED!", 40)/2, SCREEN_H/2 - 80, 40, COL_NEON_GREEN);
        DrawText("THE REACTOR HAS BEEN DEFUSED", SCREEN_W/2 - MeasureText("THE REACTOR HAS BEEN DEFUSED", 18)/2, SCREEN_H/2 - 25, 18, WHITE);
        
        DrawText("PRESS ENTER TO RETRY OR ESC FOR MENU", SCREEN_W/2 - MeasureText("PRESS ENTER TO RETRY OR ESC FOR MENU", 16)/2, SCREEN_H/2 + 50, 16, COL_NEON_BLUE);

        if (IsKeyPressed(KEY_ENTER)) {
            InitGame(g);
            g->screen = SCREEN_PLAY;
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            g->screen = SCREEN_MENU;
        }
        return;
    }

    /* ── SCREEN_PLAY RENDERING ── */
    ClearBackground(lv->bg_color);

    /* ── Parallax Background Drawing ── */
    float cam_x = g->camera.target.x;
    
    // Tiled biological background for all levels (colored/tinted per level)
    float bg_scroll = cam_x * 0.2f;
    int tile_w = 144;
    int tile_h = 144;
    int cols = (SCREEN_W / tile_w) + 2;
    int rows = (SCREEN_H / tile_h) + 1;
    float start_x = -fmodf(bg_scroll, tile_w);
    
    Color bg_tint = WHITE;
    if (g->current_level == 0) bg_tint = (Color){ 60, 160, 100, 255 };    // Stage 1: Green forest/jungle hive
    else if (g->current_level == 1) bg_tint = (Color){ 180, 110, 60, 255 };   // Stage 2: Orange refinery hive
    else if (g->current_level == 2) bg_tint = WHITE;                           // Stage 3: Pink core hive (default)

    for (int x = -1; x < cols; x++) {
        for (int y = 0; y < rows; y++) {
            DrawTexture(texBackground, x * tile_w + start_x, y * tile_h, bg_tint);
        }
    }

    BeginMode2D(g->camera);

    /* ── Draw Level Platforms ── */
    for (int i = 0; i < lv->platform_count; i++) {
        Platform plat = lv->platforms[i];
        if (plat.is_hazard) {
            // Animated hazard lava/spike pits
            // Draw a solid dark red base
            DrawRectangleRec(plat.rect, (Color){ 60, 10, 20, 255 });
            
            // Draw glowing spikes!
            float spike_w = 10.0f;
            int spike_count = plat.rect.width / spike_w;
            float pulse = sinf(t * 10.0f) * 0.5f + 0.5f;
            Color spike_color = (Color){ 255, 40, (unsigned char)(100 + pulse * 155), 255 };
            
            for (int s = 0; s < spike_count; s++) {
                Vector2 p1 = { plat.rect.x + s * spike_w, plat.rect.y + plat.rect.height };
                Vector2 p2 = { plat.rect.x + (s + 0.5f) * spike_w, plat.rect.y };
                Vector2 p3 = { plat.rect.x + (s + 1.0f) * spike_w, plat.rect.y + plat.rect.height };
                DrawTriangle(p1, p3, p2, spike_color); // pointing UP spikes
            }
            
            // Neon top line
            DrawLineEx((Vector2){ plat.rect.x, plat.rect.y }, (Vector2){ plat.rect.x + plat.rect.width, plat.rect.y }, 2.0f, spike_color);
        } else {
            // Draw organic tiled platform using Living Tissue assets (tinted by platform color)
            float x = plat.rect.x;
            float y = plat.rect.y;
            float w = plat.rect.width;
            float h = plat.rect.height;

            int tiles_x = (int)(w / 32.0f);
            if (w > 0 && tiles_x < 1) tiles_x = 1;

            for (int tx = 0; tx < tiles_x; tx++) {
                float tile_x_pos = x + tx * 32.0f;
                float draw_w = 32.0f;
                if (tx == tiles_x - 1) {
                    draw_w = w - tx * 32.0f; // fit remainder
                }
                if (draw_w <= 0) continue;

                // Select tileset source rect
                float src_x = 64.0f; // default middle
                if (tx == 0) src_x = 32.0f; // left corner top
                else if (tx == tiles_x - 1) src_x = 192.0f; // right corner top
                else {
                    src_x = 64.0f + (tx % 4) * 32.0f; // cycle repeating middle frames
                }

                Rectangle src_rect = { src_x, 96.0f, draw_w, 32.0f };
                Rectangle dest_rect = { tile_x_pos, y, draw_w, h }; // stretched vertically to fit platform height
                DrawTexturePro(texTileset, src_rect, dest_rect, (Vector2){0,0}, 0.0f, plat.color);
            }
        }
    }

    /* ── Draw Exit Sign ── */
    if (!g->boss_active) {
        float exit_x = lv->length - 80.0f;
        DrawRectangle(exit_x, 440, 12, 80, LIGHTGRAY);
        DrawRectangle(exit_x + 60, 440, 12, 80, LIGHTGRAY);
        DrawRectangle(exit_x - 10, 410, 92, 30, (Color){ 20, 80, 30, 255 });
        DrawRectangleLines(exit_x - 10, 410, 92, 30, COL_NEON_GREEN);
        
        float blink = sinf(t * 8.0f);
        DrawText("EXIT ->", exit_x, 420, 12, blink > 0 ? COL_NEON_GREEN : DARKGREEN);
    }

    /* ── Draw Item Capsules ── */
    for (int i = 0; i < MAX_CAPSULES; i++) {
        Capsule *c = &g->capsules[i];
        if (!c->active) continue;

        DrawRectangle(c->pos.x, c->pos.y, 40, 24, (Color){ 30, 50, 100, 200 });
        DrawRectangleLinesEx((Rectangle){ c->pos.x, c->pos.y, 40, 24 }, 2.0f, COL_NEON_BLUE);
        
        char label[2] = "S";
        if (c->drops_weapon == WEAPON_LASER) strcpy(label, "L");
        if (c->drops_weapon == WEAPON_FLAME) strcpy(label, "F");

        float pulse = sinf(t * 10.0f) * 3.0f;
        DrawCircle(c->pos.x + 20, c->pos.y + 12, 10 + pulse/2.0f, (Color){ 0, 100, 255, 60 });
        DrawText(label, c->pos.x + 16, c->pos.y + 6, 12, COL_NEON_BLUE);
    }

    /* ── Draw Enemies ── */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g->enemies[i];
        if (!e->active) continue;

        bool flash = (e->hit_flash > 0);

        switch (e->type) {
            case ENEMY_RUNNER: {
                // Runner Cyber-soldier from the sprite sheet
                float dw = 56.0f;
                float dh = 56.0f;
                float dx = e->pos.x + e->size.x / 2.0f;
                float dy = e->pos.y + e->size.y - dh / 2.0f; // ground-anchored

                int frame = (int)(t * 12.0f + e->pos.x * 0.03f) % 6;
                float src_x = frame * 48.0f;
                float src_y = 48.0f; // Running forward Y row
                float src_w = e->facing_right ? 48.0f : -48.0f;
                Rectangle source_rect = { src_x, src_y, src_w, 48.0f };

                Color tint = flash ? WHITE : (Color){ 255, 100, 100, 255 }; // Crimson Cyber tint
                DrawTexturePro(texPlayerSheet, source_rect, (Rectangle){ dx, dy, dw, dh }, (Vector2){ dw/2.0f, dh/2.0f }, 0.0f, tint);
                break;
            }
            case ENEMY_SNIPER: {
                // Crouching Sniper from the sprite sheet
                float dw = 56.0f;
                float dh = 56.0f;
                float dx = e->pos.x + e->size.x / 2.0f;
                float dy = e->pos.y + e->size.y - dh / 2.0f; // ground-anchored

                float src_x = 0.0f; // Crouching aiming forward Column 0
                float src_y = 96.0f; // Y row for crouching
                float src_w = e->facing_right ? 48.0f : -48.0f;
                Rectangle source_rect = { src_x, src_y, src_w, 48.0f };

                Color tint = flash ? WHITE : (Color){ 240, 100, 255, 255 }; // Violet Cyber tint
                DrawTexturePro(texPlayerSheet, source_rect, (Rectangle){ dx, dy, dw, dh }, (Vector2){ dw/2.0f, dh/2.0f }, 0.0f, tint);
                break;
            }
            case ENEMY_TURRET: {
                // Rotating wall turret aims directly at player (using texTurret)
                Color tint = flash ? COL_NEON_ORANGE : WHITE;
                
                DrawTexturePro(texTurret, (Rectangle){ 0, 0, 64, 64 }, (Rectangle){ e->pos.x + e->size.x/2.0f, e->pos.y + e->size.y/2.0f, e->size.x, e->size.y }, (Vector2){ e->size.x/2.0f, e->size.y/2.0f }, 0.0f, tint);
                
                // Draw rotating gun barrel towards player
                Vector2 dir = { p->pos.x + p->size.x/2 - e->pos.x - 16, p->pos.y + p->size.y/2 - e->pos.y - 16 };
                float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
                if (len > 0) {
                    Vector2 barrel = (Vector2){ e->pos.x + 16 + (dir.x/len)*24, e->pos.y + 16 + (dir.y/len)*24 };
                    DrawLineEx((Vector2){e->pos.x + 16, e->pos.y + 16}, barrel, 4.0f, flash ? COL_NEON_ORANGE : (Color){ 250, 160, 50, 255 });
                }
                break;
            }
            case ENEMY_BOSS_LEFT_TURRET:
            case ENEMY_BOSS_RIGHT_TURRET: {
                Color body_col = flash ? WHITE : (Color){ 100, 100, 120, 255 };

                DrawRectangle(e->pos.x, e->pos.y, e->size.x, e->size.y, body_col);
                DrawRectangleLinesEx((Rectangle){ e->pos.x, e->pos.y, e->size.x, e->size.y }, 2.0f, flash ? WHITE : COL_NEON_ORANGE);
                
                Vector2 dir = { p->pos.x + p->size.x/2 - e->pos.x, p->pos.y + p->size.y/2 - e->pos.y };
                float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
                if (len > 0) {
                    Vector2 barrel = (Vector2){ e->pos.x + 20 + (dir.x/len)*36, e->pos.y + 20 + (dir.y/len)*36 };
                    DrawLineEx((Vector2){e->pos.x + 20, e->pos.y + 20}, barrel, 6.0f, flash ? WHITE : COL_NEON_ORANGE);
                }

                // Health mini-bar
                DrawRectangle((int)e->pos.x - 5, (int)e->pos.y - 10, 50, 4, RED);
                DrawRectangle((int)e->pos.x - 5, (int)e->pos.y - 10, (int)(50 * (e->health / e->max_health)), 4, GREEN);
                break;
            }
            case ENEMY_BOSS_CORE: {
                Color body_col = flash ? WHITE : (Color){ 60, 60, 80, 255 };

                DrawRectangle((int)e->pos.x, (int)e->pos.y, (int)e->size.x, (int)e->size.y, body_col);
                DrawRectangleLinesEx((Rectangle){ e->pos.x, e->pos.y, e->size.x, e->size.y }, 3.0f, flash ? WHITE : COL_NEON_PINK);
                
                if (e->boss_core_open) {
                    float pulse = sinf(t * 15.0f) * 6.0f;
                    DrawCircle((int)e->pos.x + 32, (int)e->pos.y + 40, (int)(20 + pulse/2), RED);
                    DrawCircle((int)e->pos.x + 32, (int)e->pos.y + 40, (int)(12 + pulse/3), COL_NEON_PINK);
                } else {
                    DrawRectangle((int)e->pos.x + 10, (int)e->pos.y + 10, 44, 60, flash ? WHITE : (Color){ 100, 100, 110, 255 });
                    DrawRectangleLinesEx((Rectangle){ e->pos.x + 10, e->pos.y + 10, 44, 60 }, 2.0f, DARKGRAY);
                }

                // Boss main health bar
                DrawRectangle((int)e->pos.x - 20, (int)e->pos.y - 18, 104, 6, RED);
                DrawRectangle((int)e->pos.x - 20, (int)e->pos.y - 18, (int)(104 * (e->health / e->max_health)), 6, COL_NEON_PINK);
                break;
            }
        }
    }

    /* ── Draw Player Commando ── */
    if (p->alive) {
        bool draw_p = true;
        if (p->invuln_timer > 0) {
            draw_p = ((int)(p->invuln_timer * 15.0f) % 2 == 0);
        }

        if (draw_p) {
            // Calculate player aim vector first, so we can use it for sprite frame selection!
            Vector2 aim = { 1.0f, 0.0f };
            if (p->facing_right) aim.x = 1.0f;
            else aim.x = -1.0f;

            float adx = 0, ady = 0;
            if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) adx -= 1.0f;
            if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) adx += 1.0f;
            if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) ady -= 1.0f;
            if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) ady += 1.0f;

            bool is_crouching = p->on_ground && (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S));

            if (is_crouching) {
                if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) {
                    aim.y = -1.0f;
                    if (adx != 0) aim.x = adx;
                    else aim.x = 0;
                } else {
                    aim.y = 0;
                    if (adx != 0) aim.x = adx;
                }
            } else {
                if (ady < 0) {
                    aim.y = -1.0f;
                    if (adx == 0) aim.x = 0;
                } else if (ady > 0 && !p->on_ground) {
                    aim.y = 1.0f;
                    if (adx == 0) aim.x = 0;
                } else {
                    aim.y = 0;
                    if (adx != 0) aim.x = adx;
                }
            }
            float aim_len = sqrtf(aim.x*aim.x + aim.y*aim.y);
            if (aim_len > 0) { aim.x /= aim_len; aim.y /= aim_len; }

            // Calculate squashed & stretched size and position (drawn at 64x64 to make the sprite larger and prevent squashing)
            float dw = 64.0f * p->squash_x;
            float dh = 64.0f * p->squash_y;
            float dx = p->pos.x + p->size.x / 2.0f;
            float dy = p->pos.y + p->size.y - dh / 2.0f; // ground-anchored

            // Sprite Sheet animation frame selection logic
            float src_x = 0;
            float src_y = 0;
            
            if (!p->on_ground) {
                // Somersault roll spin in mid-air
                int frame = (int)(t * 15.0f) % 3;
                src_x = frame * 48.0f;
                src_y = 144.0f;
            } else if (is_crouching) {
                // Crouching aiming
                if (aim.x == 0 && aim.y == -1.0f) {
                    src_x = 2 * 48.0f; // Crouching aiming straight UP
                    src_y = 96.0f;
                } else if (aim.x != 0 && aim.y == -1.0f) {
                    src_x = 1 * 48.0f; // Crouching aiming diagonally UP
                    src_y = 96.0f;
                } else {
                    src_x = 0 * 48.0f; // Crouching aiming forward
                    src_y = 96.0f;
                }
            } else {
                // On Ground
                if (p->vel.x == 0) {
                    // Idle aiming
                    if (aim.x == 0 && aim.y == -1.0f) {
                        src_x = 2 * 48.0f; // Aiming straight UP
                        src_y = 0.0f;
                    } else if (aim.x != 0 && aim.y == -1.0f) {
                        src_x = 1 * 48.0f; // Aiming diagonally UP
                        src_y = 0.0f;
                    } else {
                        // Standing forward idle loop (breathe effect: frames 0, 3, 4)
                        int idle_frames[3] = { 0, 3, 4 };
                        int frame = (int)(t * 3.0f) % 3;
                        src_x = idle_frames[frame] * 48.0f;
                        src_y = 0.0f;
                    }
                } else {
                    // Running
                    if (aim.y == -1.0f) {
                        // Running diagonally UP: Y=96.0f, Columns 3, 4, 5
                        int diag_frames[3] = { 3, 4, 5 };
                        int frame = (int)(t * 12.0f) % 3;
                        src_x = diag_frames[frame] * 48.0f;
                        src_y = 96.0f;
                    } else {
                        int frame = (int)(t * 12.0f) % 6;
                        src_x = frame * 48.0f;
                        src_y = 48.0f; // Running forward
                    }
                }
            }
            
            float src_w = p->facing_right ? 48.0f : -48.0f;
            Rectangle source_rect = { src_x, src_y, src_w, 48.0f };

            // Draw dash ghosts using player sprite poses
            if (p->dash_timer > 0) {
                for (int k = 0; k < p->dash_ghost_count; k++) {
                    float alpha = (1.0f - (float)(k + 1) / 4.0f) * 0.4f;
                    Color ghost_col = (Color){ 0, 180, 255, (unsigned char)(255 * alpha) };
                    float g_dx = p->dash_ghosts[k].x + p->size.x/2.0f;
                    float g_dy = p->dash_ghosts[k].y + p->size.y - dh/2.0f;
                    DrawTexturePro(texPlayerSheet, source_rect, (Rectangle){ g_dx, g_dy, dw, dh }, (Vector2){ dw/2.0f, dh/2.0f }, p->jump_rotation, ghost_col);
                }
            }

            // Draw player texture from the sprite sheet
            DrawTexturePro(texPlayerSheet, source_rect, (Rectangle){ dx, dy, dw, dh }, (Vector2){ dw/2.0f, dh/2.0f }, p->jump_rotation, WHITE);
        }
    }

    /* ── Draw Player Bullets ── */
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &g->bullets[i];
        if (!b->active) continue;

        Color bc = COL_NEON_BLUE;
        float b_size = 4.0f;

        if (b->type == WEAPON_SPREAD) { bc = COL_NEON_PINK; b_size = 6.0f; }
        if (b->type == WEAPON_LASER) { bc = COL_NEON_GREEN; b_size = 8.0f; }
        if (b->type == WEAPON_FLAME) { bc = COL_NEON_ORANGE; b_size = 7.0f; }

        if (b->type == WEAPON_LASER) {
            // Draw stretched beam along angle
            Vector2 end = (Vector2){ b->pos.x - cosf(b->angle)*20, b->pos.y - sinf(b->angle)*20 };
            DrawLineEx(b->pos, end, 3.0f, bc);
        } else {
            DrawCircle((int)b->pos.x, (int)b->pos.y, b_size, bc);
            DrawCircle((int)b->pos.x, (int)b->pos.y, b_size * 0.4f, WHITE); // bright core
        }
    }

    /* ── Draw Enemy Bullets ── */
    for (int i = 0; i < MAX_ENEMY_BULS; i++) {
        EnemyBullet *eb = &g->enemy_bullets[i];
        if (!eb->active) continue;

        DrawCircle((int)eb->pos.x, (int)eb->pos.y, 4, COL_NEON_PINK);
        DrawCircle((int)eb->pos.x, (int)eb->pos.y, 1.5f, WHITE);
    }

    /* ── Draw Particles ── */
    DrawParticles(g);

    EndMode2D();

    /* ── Draw Heads Up Display (HUD) ── */
    // Translucent glass panel behind HUD
    DrawRectangle(10, 10, 360, 100, (Color){ 16, 12, 28, 200 });
    DrawRectangleLines(10, 10, 360, 100, COL_CYBER_PURPLE);
    DrawRectangle(12, 12, 356, 2, (Color){ 255, 255, 255, 80 }); // highlights

    // Title header
    DrawText(lv->name, 20, 20, 16, COL_NEON_BLUE);

    // Shield Bar
    DrawRectangle(20, 50, 200, 16, (Color){ 60, 20, 40, 180 });
    DrawRectangle(20, 50, (int)(200 * (p->shield / p->max_shield)), 16, COL_NEON_GREEN);
    DrawRectangleLines(20, 50, 200, 16, WHITE);
    DrawText("SHIELD", 28, 52, 12, BLACK);

    // Lives counter
    char lives_txt[32];
    snprintf(lives_txt, sizeof(lives_txt), "LIVES: %d", p->lives);
    DrawText(lives_txt, 240, 44, 16, COL_NEON_PINK);

    // Kill counter
    char kills_txt[32];
    snprintf(kills_txt, sizeof(kills_txt), "KILLS: %d", g->kill_count);
    DrawText(kills_txt, 240, 64, 14, COL_NEON_BLUE);

    // Weapon Type label
    char weap_txt[64];
    switch (p->weapon) {
        case WEAPON_RIFLE: strcpy(weap_txt, "WEAPON: RIFLE"); break;
        case WEAPON_SPREAD: strcpy(weap_txt, "WEAPON: SPREAD (S)"); break;
        case WEAPON_LASER: strcpy(weap_txt, "WEAPON: LIGHT-LASER (L)"); break;
        case WEAPON_FLAME: strcpy(weap_txt, "WEAPON: FLAME-SPIRAL (F)"); break;
    }
    DrawText(weap_txt, 20, 80, 14, COL_NEON_BLUE);

    // Boss Alert Notification banner
    if (g->boss_active) {
        float alert_blink = sinf(t * 10.0f);
        if (alert_blink > 0) {
            DrawText("!!! BOSS INTRUSION DETECTED !!!", SCREEN_W/2 - MeasureText("!!! BOSS INTRUSION DETECTED !!!", 24)/2, 80, 24, RED);
        }
    }

    // CRT Scanlines Overlay
    for (int y = 0; y < SCREEN_H; y += 3) {
        DrawLine(0, y, SCREEN_W, y, (Color){ 0, 0, 0, 30 });
    }
    
    // Vignette shadow
    DrawRectangleGradientV(0, 0, SCREEN_W, 40, (Color){ 0, 0, 0, 180 }, (Color){ 0, 0, 0, 0 });
    DrawRectangleGradientV(0, SCREEN_H - 40, SCREEN_W, 40, (Color){ 0, 0, 0, 0 }, (Color){ 0, 0, 0, 180 });
}

/* ─────────────────────────── Main Loop ─────────────────────────── */

int main(void) {
    InitWindow(SCREEN_W, SCREEN_H, "Cyber Commando");
    SetTargetFPS(60);
    SetRandomSeed((unsigned int)GetTime());

    // Initialize Audio Devices and stream
    InitAudioDevice();
    AudioStream stream = LoadAudioStream(44100, 16, 1);
    SetAudioStreamCallback(stream, SynthAudioCallback);
    PlayAudioStream(stream);

    // Generate Sprites
    texPlayer = GeneratePlayerTexture();
    texRunner = GenerateRunnerTexture();
    texSniper = GenerateSniperTexture();
    texTurret = GenerateTurretTexture();
    texPlayerSheet = LoadTexture("contra clone character sprite sheet.png");
    texTileset = LoadTexture("Living-Tissue-Platform-Files/PNG/layers/tileset.png");
    texBackground = LoadTexture("Living-Tissue-Platform-Files/PNG/layers/bakground.png");

    GameState g = {0};
    InitGame(&g);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;

        // Global back key
        if (IsKeyPressed(KEY_ESCAPE) && g.screen == SCREEN_PLAY) {
            g.screen = SCREEN_MENU;
        }

        UpdateGame(&g, dt);

        BeginDrawing();
        ClearBackground(BLACK);

        DrawGame(&g);

        EndDrawing();
    }

    // Unload textures
    UnloadTexture(texPlayer);
    UnloadTexture(texRunner);
    UnloadTexture(texSniper);
    UnloadTexture(texTurret);
    UnloadTexture(texPlayerSheet);
    UnloadTexture(texTileset);
    UnloadTexture(texBackground);

    // Close Audio device
    UnloadAudioStream(stream);
    CloseAudioDevice();

    CloseWindow();
    return 0;
}
