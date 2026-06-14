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
#define SCREEN_H        600
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
        case 0: // Cyber Jungle
            strcpy(lv->name, "STAGE 1: CYBER OUTPOST");
            lv->length = 3200.0f;
            lv->spawn_point = (Vector2){ 100, 450 };
            lv->boss_trigger_point = (Vector2){ 0, 0 }; // No boss on level 1
            lv->bg_color = (Color){ 12, 18, 15, 255 };
            lv->platform_color = COL_NEON_GREEN;

            // Ground floors
            AddPlatform(lv, 0, 520, 800, 80, COL_NEON_GREEN, false);
            AddPlatform(lv, 900, 520, 1000, 80, COL_NEON_GREEN, false);
            AddPlatform(lv, 2050, 520, 1200, 80, COL_NEON_GREEN, false);
            // Hazard pits
            AddPlatform(lv, 800, 560, 100, 40, COL_NEON_PINK, true);
            AddPlatform(lv, 1900, 560, 150, 40, COL_NEON_PINK, true);

            // Elevated platforms
            AddPlatform(lv, 300, 400, 200, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 600, 320, 200, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 1050, 400, 300, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 1400, 300, 250, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 1750, 400, 200, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 2200, 330, 300, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 2600, 420, 250, 20, COL_NEON_BLUE, false);

            // Spawning capsules
            SpawnCapsule(g, 700, 150, WEAPON_SPREAD);
            SpawnCapsule(g, 1800, 150, WEAPON_LASER);

            // Spawning enemies
            SpawnEnemy(g, ENEMY_SNIPER, 400, 368);
            SpawnEnemy(g, ENEMY_RUNNER, 1200, 480);
            SpawnEnemy(g, ENEMY_TURRET, 1450, 300);
            SpawnEnemy(g, ENEMY_SNIPER, 1550, 268);
            SpawnEnemy(g, ENEMY_RUNNER, 2300, 290);
            SpawnEnemy(g, ENEMY_TURRET, 2400, 330);
            SpawnEnemy(g, ENEMY_RUNNER, 2700, 480);
            SpawnEnemy(g, ENEMY_SNIPER, 2800, 488);
            break;

        case 1: // Reactor Refinery
            strcpy(lv->name, "STAGE 2: REACTOR REFINERY");
            lv->length = 3600.0f;
            lv->spawn_point = (Vector2){ 100, 400 };
            lv->boss_trigger_point = (Vector2){ 0, 0 };
            lv->bg_color = (Color){ 20, 15, 10, 255 };
            lv->platform_color = COL_NEON_ORANGE;

            // Step layout refinery
            AddPlatform(lv, 0, 480, 600, 120, COL_NEON_ORANGE, false);
            AddPlatform(lv, 750, 520, 800, 80, COL_NEON_ORANGE, false);
            AddPlatform(lv, 1700, 460, 600, 140, COL_NEON_ORANGE, false);
            AddPlatform(lv, 2450, 520, 1200, 80, COL_NEON_ORANGE, false);

            // Hazard refinery pits
            AddPlatform(lv, 600, 570, 150, 30, COL_NEON_PINK, true);
            AddPlatform(lv, 1550, 570, 150, 30, COL_NEON_PINK, true);
            AddPlatform(lv, 2300, 570, 150, 30, COL_NEON_PINK, true);

            // High refinery catwalks
            AddPlatform(lv, 250, 350, 180, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 500, 260, 220, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 850, 380, 200, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 1150, 280, 250, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 1800, 320, 220, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 2100, 220, 250, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 2600, 380, 300, 20, COL_NEON_BLUE, false);

            // Capsules
            SpawnCapsule(g, 600, 100, WEAPON_FLAME);
            SpawnCapsule(g, 2000, 100, WEAPON_SPREAD);
            SpawnCapsule(g, 2900, 150, WEAPON_LASER);

            // Enemies
            SpawnEnemy(g, ENEMY_TURRET, 350, 350);
            SpawnEnemy(g, ENEMY_SNIPER, 600, 228);
            SpawnEnemy(g, ENEMY_RUNNER, 900, 480);
            SpawnEnemy(g, ENEMY_SNIPER, 1250, 248);
            SpawnEnemy(g, ENEMY_TURRET, 1850, 320);
            SpawnEnemy(g, ENEMY_RUNNER, 1900, 420);
            SpawnEnemy(g, ENEMY_SNIPER, 2200, 188);
            SpawnEnemy(g, ENEMY_TURRET, 2700, 380);
            SpawnEnemy(g, ENEMY_RUNNER, 3000, 480);
            break;

        case 2: // Alien Hive Core (Boss Stage)
            strcpy(lv->name, "STAGE 3: THE HEART OF CYBER-CORE");
            lv->length = 2048.0f;
            lv->spawn_point = (Vector2){ 100, 450 };
            lv->boss_trigger_point = (Vector2){ 1200.0f, 0.0f }; // Triggers boss lock at X = 1200
            lv->bg_color = (Color){ 24, 10, 18, 255 };
            lv->platform_color = COL_NEON_PINK;

            // Ground floor leading to the boss
            AddPlatform(lv, 0, 520, 1400, 80, COL_NEON_PINK, false);
            
            // Floating sniper platform before boss
            AddPlatform(lv, 300, 400, 200, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 600, 300, 200, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 900, 400, 300, 20, COL_NEON_BLUE, false);

            // Boss arena platforming layout
            AddPlatform(lv, 1200, 360, 200, 20, COL_NEON_BLUE, false);
            AddPlatform(lv, 1200, 220, 200, 20, COL_NEON_BLUE, false);

            // Pre-boss capsules
            SpawnCapsule(g, 500, 150, WEAPON_SPREAD);
            SpawnCapsule(g, 1000, 150, WEAPON_FLAME);

            // Snipers in approach
            SpawnEnemy(g, ENEMY_SNIPER, 400, 368);
            SpawnEnemy(g, ENEMY_SNIPER, 700, 268);
            SpawnEnemy(g, ENEMY_SNIPER, 1000, 368);
            break;
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
                    e->health = 250.0f;
                    break;
                case ENEMY_BOSS_CORE:
                    e->size = (Vector2){ 64, 80 };
                    e->health = 600.0f;
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

    Vector2 spawn_pos = (Vector2){ p->pos.x + p->size.x/2.0f + aim.x * 20.0f, p->pos.y + p->size.y/2.5f + aim.y * 20.0f };

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
            p->pos = g->levels[g->current_level].spawn_point;
            p->vel = (Vector2){ 0, 0 };
            p->jump_rotation = 0;
            p->squash_x = 1.0f;
            p->squash_y = 1.0f;
            p->last_safe_ground_pos = g->levels[g->current_level].spawn_point;
        }
    }
}

/* ─────────────────────────── Update Game ─────────────────────────── */

static void UpdateGame(GameState *g, float dt) {
    Level *lv = &g->levels[g->current_level];
    Player *p = &g->player;

    if (g->screen != SCREEN_PLAY) return;

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
            // Spawn Boss segments on the right edge of stage screen
            float boss_x = lv->length - 150.0f;
            SpawnEnemy(g, ENEMY_BOSS_LEFT_TURRET, boss_x + 10, 200);
            SpawnEnemy(g, ENEMY_BOSS_RIGHT_TURRET, boss_x + 10, 420);
            SpawnEnemy(g, ENEMY_BOSS_CORE, boss_x + 16, 290);
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
        p->vel.x = move_x * MOVE_SPEED;

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
    Vector2 aim = { 1.0f, 0.0f };
    if (p->facing_right) aim.x = 1.0f;
    else aim.x = -1.0f;

    float adx = 0, ady = 0;
    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) adx -= 1.0f;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) adx += 1.0f;
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) ady -= 1.0f;
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) ady += 1.0f;

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
    // Normalize
    float aim_len = sqrtf(aim.x*aim.x + aim.y*aim.y);
    if (aim_len > 0) { aim.x /= aim_len; aim.y /= aim_len; }

    /* ── Player Shoot ── */
    if (IsKeyDown(KEY_J) || IsKeyDown(KEY_X)) {
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
                if (CheckCollisionRecs2(e->pos, e->size, p->pos, p->size)) {
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
                if (e->shoot_timer >= 1.8f) {
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
                                ebul->vel = (Vector2){ cosf(ang) * 300.0f, sinf(ang) * 300.0f };
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
                if (e->shoot_timer >= 1.2f) {
                    e->shoot_timer = 0;
                    PlaySynthSFX(800.0f, 400.0f, 0.15f, 0.25f, false);
                    // Direct fire laser bullet
                    for (int eb = 0; eb < MAX_ENEMY_BULS; eb++) {
                        if (!g->enemy_bullets[eb].active) {
                            EnemyBullet *ebul = &g->enemy_bullets[eb];
                            ebul->pos = e->pos;
                            ebul->vel = (Vector2){ (dir.x/len) * 450.0f, (dir.y/len) * 450.0f };
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
                    if (e->shoot_timer >= 0.5f) {
                        e->shoot_timer = 0;
                        PlaySynthSFX(300.0f, 100.0f, 0.25f, 0.35f, true); // fireball crackle
                        // Spits fireballs straight left
                        for (int eb = 0; eb < MAX_ENEMY_BULS; eb++) {
                            if (!g->enemy_bullets[eb].active) {
                                EnemyBullet *ebul = &g->enemy_bullets[eb];
                                ebul->pos = (Vector2){ e->pos.x, e->pos.y + e->size.y/2 };
                                ebul->vel = (Vector2){ -280.0f, (float)GetRandomValue(-100, 100) };
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
                    
                    // Explosion SFX
                    PlaySynthSFX(160.0f, 35.0f, 0.42f, 0.75f, true);

                    // Explosion particles
                    AddParticleEx(g, (Vector2){ e->pos.x + e->size.x/2, e->pos.y + e->size.y/2 }, COL_NEON_ORANGE, 200.0f, 5.0f, PARTICLE_SPARK, 0.6f, 25);
                    g->camera_shake = 0.22f;

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
        if (CheckCollisionRecs2(eb->pos, (Vector2){8,8}, p->pos, p->size)) {
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
        // Lock camera focused on the boss arena layout
        target_x = lv->boss_trigger_point.x + SCREEN_W / 3.0f;
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
    
    // Twinkling Star Sky
    for (int i = 0; i < 60; i++) {
        float star_x = fmodf(i * 123.456f - cam_x * 0.05f, SCREEN_W + 100.0f) - 50.0f;
        float star_y = fmodf(i * 987.654f, SCREEN_H * 0.5f);
        float blink = sinf(t * 2.0f + i) * 0.5f + 0.5f;
        Color sc = (Color){ 200, 220, 255, (unsigned char)(60 + blink * 180) };
        DrawCircle((int)star_x, (int)star_y, i % 3 == 0 ? 1.5f : 1.0f, sc);
    }
    
    // Far BG Skyline layers (Skyscrapers with neon windows)
    for (int i = 0; i < 8; i++) {
        float offset_x = fmodf(-cam_x * 0.15f + i * 220.0f, 1980.0f) - 220.0f;
        float height = 240.0f + sinf(i * 1.5f) * 80.0f;
        float width = 150.0f;
        float top_y = SCREEN_H - height;
        
        DrawRectangle((int)offset_x, (int)top_y, (int)width, (int)height, (Color){ 22, 16, 32, 255 });
        DrawRectangleLines((int)offset_x, (int)top_y, (int)width, (int)height, (Color){ 52, 32, 68, 255 });
        
        // Neon windows
        for (int wx = 15; wx < width - 15; wx += 25) {
            for (int wy = 20; wy < height - 20; wy += 35) {
                int win_val = (i * 9 + wx * 17 + wy * 7) % 12;
                if (win_val < 3) {
                    Color win_col = COL_NEON_BLUE;
                    if (win_val == 1) win_col = COL_NEON_PINK;
                    if (win_val == 2) win_col = COL_NEON_ORANGE;
                    win_col.a = 70; // transparency
                    DrawRectangle((int)(offset_x + wx), (int)(top_y + wy), 10, 14, win_col);
                }
            }
        }
    }

    // Mid BG Industrial layers (diagonal lattice grids)
    for (int i = 0; i < 10; i++) {
        float offset_x = fmodf(-cam_x * 0.35f + i * 180.0f, 1800.0f) - 180.0f;
        float height = 160.0f + cosf(i * 2.1f) * 50.0f;
        float width = 110.0f;
        float top_y = SCREEN_H - height;
        
        DrawRectangle((int)offset_x, (int)top_y, (int)width, (int)height, (Color){ 28, 22, 40, 255 });
        DrawRectangleLines((int)offset_x, (int)top_y, (int)width, (int)height, (Color){ 76, 52, 102, 255 });
        
        // Crossbars details
        DrawLine((int)offset_x, (int)top_y, (int)(offset_x + width), (int)(top_y + height), (Color){ 76, 52, 102, 50 });
        DrawLine((int)(offset_x + width), (int)top_y, (int)offset_x, (int)(top_y + height), (Color){ 76, 52, 102, 50 });
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
            // Normal solid platform
            DrawRectangleRec(plat.rect, lv->platform_color);
            // Draw a thick, highly visible neon top border line
            DrawLineEx((Vector2){ plat.rect.x, plat.rect.y }, (Vector2){ plat.rect.x + plat.rect.width, plat.rect.y }, 4.0f, WHITE);
            // Draw a glowing platform color outline
            DrawRectangleLinesEx(plat.rect, 2.0f, lv->platform_color);
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
                // Running soldier (using texRunner with horizontal flip and hit-flash colors)
                float src_w = e->facing_right ? 64.0f : -64.0f;
                Color tint = flash ? COL_NEON_ORANGE : WHITE;
                
                // Simple run cycle oscillation for legs
                float leg_swing = sinf(t * 16.0f) * 8.0f;
                
                DrawTexturePro(texRunner, (Rectangle){ 0, 0, src_w, 64 }, (Rectangle){ e->pos.x + e->size.x/2.0f, e->pos.y + e->size.y/2.0f, e->size.x, e->size.y }, (Vector2){ e->size.x/2.0f, e->size.y/2.0f }, 0.0f, tint);
                
                // Draw animated legs underneath
                if (flash) {
                    DrawLineEx((Vector2){ e->pos.x + 8, e->pos.y + e->size.y - 10 }, (Vector2){ e->pos.x + 8 + leg_swing, e->pos.y + e->size.y }, 4.0f, COL_NEON_ORANGE);
                    DrawLineEx((Vector2){ e->pos.x + 24, e->pos.y + e->size.y - 10 }, (Vector2){ e->pos.x + 24 - leg_swing, e->pos.y + e->size.y }, 4.0f, COL_NEON_ORANGE);
                } else {
                    DrawLineEx((Vector2){ e->pos.x + 8, e->pos.y + e->size.y - 10 }, (Vector2){ e->pos.x + 8 + leg_swing, e->pos.y + e->size.y }, 4.0f, (Color){ 160, 40, 40, 255 });
                    DrawLineEx((Vector2){ e->pos.x + 24, e->pos.y + e->size.y - 10 }, (Vector2){ e->pos.x + 24 - leg_swing, e->pos.y + e->size.y }, 4.0f, (Color){ 160, 40, 40, 255 });
                }
                break;
            }
            case ENEMY_SNIPER: {
                // Standing sniper with laser pointer (using texSniper and hit-flash colors)
                float src_w = e->facing_right ? 64.0f : -64.0f;
                Color tint = flash ? COL_NEON_ORANGE : WHITE;
                
                DrawTexturePro(texSniper, (Rectangle){ 0, 0, src_w, 64 }, (Rectangle){ e->pos.x + e->size.x/2.0f, e->pos.y + e->size.y/2.0f, e->size.x, e->size.y }, (Vector2){ e->size.x/2.0f, e->size.y/2.0f }, 0.0f, tint);

                // Laser aim guide line (telegraphed warning!)
                Vector2 target_pos = (Vector2){ p->pos.x + p->size.x/2, p->pos.y + p->size.y/2 };
                Vector2 origin_pos = (Vector2){ e->pos.x + (e->facing_right ? 25 : 7), e->pos.y + 12 };
                
                float ratio = e->shoot_timer / 2.0f;
                Color laser_color;
                float line_thick = 1.0f;
                if (ratio < 0.6f) {
                    laser_color = (Color){ 50, 255, 120, 45 };
                } else if (ratio < 0.85f) {
                    laser_color = (Color){ 255, 40, 40, 95 };
                } else {
                    laser_color = (Color){ 255, 0, 0, (unsigned char)(160 + sinf(t * 30.0f) * 95) };
                    line_thick = 2.0f;
                }
                DrawLineEx(origin_pos, target_pos, line_thick, laser_color);
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
            // Draw dash ghosts if player is dashing
            if (p->dash_timer > 0) {
                for (int k = 0; k < p->dash_ghost_count; k++) {
                    float alpha = (1.0f - (float)(k + 1) / 4.0f) * 0.4f;
                    Color ghost_col = COL_NEON_BLUE;
                    ghost_col.a = (unsigned char)(255 * alpha);
                    DrawRectangle((int)p->dash_ghosts[k].x, (int)p->dash_ghosts[k].y, (int)p->size.x, (int)p->size.y, ghost_col);
                }
            }

            // Calculate squashed & stretched size and position
            float dw = p->size.x * p->squash_x;
            float dh = p->size.y * p->squash_y;
            float dx = p->pos.x + p->size.x / 2.0f;
            float dy = p->pos.y + p->size.y - dh / 2.0f; // ground-anchored

            // Draw player texture (with proper squash & stretch & somersault rotation!)
            DrawTexturePro(texPlayer, (Rectangle){ 0, 0, 64, 64 }, (Rectangle){ dx, dy, dw, dh }, (Vector2){ dw/2.0f, dh/2.0f }, p->jump_rotation, WHITE);

            // Draw Pointer weapon barrel based on keyboard aim vector
            Vector2 aim = { 1.0f, 0.0f };
            if (p->facing_right) aim.x = 1.0f;
            else aim.x = -1.0f;

            float adx = 0, ady = 0;
            if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) adx -= 1.0f;
            if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) adx += 1.0f;
            if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) ady -= 1.0f;
            if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) ady += 1.0f;

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
            float aim_len = sqrtf(aim.x*aim.x + aim.y*aim.y);
            if (aim_len > 0) { aim.x /= aim_len; aim.y /= aim_len; }

            Vector2 gun_start = (Vector2){ dx, dy - dh/6.0f };
            Vector2 gun_end = (Vector2){ gun_start.x + aim.x * 24.0f, gun_start.y + aim.y * 24.0f };
            DrawLineEx(gun_start, gun_end, 4.0f, COL_NEON_PINK);
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
    DrawText(lives_txt, 240, 50, 16, COL_NEON_PINK);

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

    // Close Audio device
    UnloadAudioStream(stream);
    CloseAudioDevice();

    CloseWindow();
    return 0;
}
