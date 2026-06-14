#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ─────────────────────────── Constants ─────────────────────────── */

#define SCREEN_W        1024
#define SCREEN_H        768
#define TILE_SIZE       48
#define PLAYER_RADIUS   18
#define ENEMY_RADIUS    18
#define VISION_DISTANCE 300.0f
#define VISION_ANGLE    70.0f   // Total FOV in degrees

#define MAX_ENEMIES     20
#define MAX_LEVELS      3
#define MAP_WIDTH       (SCREEN_W / TILE_SIZE)
#define MAP_HEIGHT      (SCREEN_H / TILE_SIZE)

/* ─────────────────────────── Enums & Structs ─────────────────────────── */

typedef enum {
    SCREEN_MENU,
    SCREEN_PLAY,
    SCREEN_WIN_LEVEL,
    SCREEN_GAME_OVER,
    SCREEN_WIN_GAME
} GameScreen;

typedef enum {
    STATE_PATROL,
    STATE_INVESTIGATE,
    STATE_CHASE
} EnemyState;

typedef enum {
    PARTICLE_SPARK,
    PARTICLE_SMOKE
} ParticleType;

typedef struct {
    Vector2 pos;
    float rotation;
    bool alive;
} Entity;

typedef struct {
    Entity base;
    float speed;
    float health;
    float max_health;
    int smoke_bombs;
    float dash_cooldown;
    float dash_timer;
    Vector2 dash_dir;
    float slash_timer;
    Vector2 slash_target;
} Player;

typedef struct {
    Entity base;
    Vector2 waypoints[4];
    int currentWaypoint;
    int numWaypoints;
    float waitTime;
    float waitTimer;

    // AI state extensions
    EnemyState state;
    Vector2 targetPos;
    float chaseTimer;
    float shoot_timer;
    float muzzle_flash_timer;
    float hit_flash;
} Enemy;

typedef struct {
    int tiles[MAP_HEIGHT][MAP_WIDTH];
    Enemy enemies[MAX_ENEMIES];
    int enemyCount;
    Vector2 playerSpawn;
} Level;

typedef struct {
    Vector2 pos;
    float size;
    Color color;
} BloodPool;

typedef struct {
    Vector2 pos;
    float radius;
    float life;
    bool active;
} SmokeCloud;

typedef struct {
    Vector2 pos;
    float radius;
    float maxRadius;
    float life;
    bool active;
} LureWave;

typedef struct {
    Vector2 start;
    Vector2 pos;
    Vector2 target;
    float t;
    bool active;
} Pebble;

typedef struct {
    Vector2 pos;
    float alpha;
} DashGhost;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    Color   color;
    float   life;
    float   max_life;
    float   size;
    ParticleType type;
} Particle;

/* ─────────────────────────── Global State ─────────────────────────── */

static GameScreen currentScreen = SCREEN_MENU;
static Player player = {0};
static Level levels[MAX_LEVELS];
static int currentLevelIdx = 0;
static Texture2D texAssassin;
static Texture2D texGuard;
static float screenShake = 0.0f;

#define MAX_BLOOD_POOLS 64
static BloodPool blood_pools[MAX_BLOOD_POOLS] = { 0 };
static int blood_pool_count = 0;

#define MAX_SMOKES 4
static SmokeCloud smoke_clouds[MAX_SMOKES] = { 0 };

static LureWave lure_wave = { 0 };
static Pebble pebble = { 0 };

#define MAX_GHOSTS 8
static DashGhost dash_ghosts[MAX_GHOSTS] = { 0 };
static int ghost_count = 0;

#define MAX_PARTICLES 128
static Particle particles[MAX_PARTICLES] = { 0 };
static int particle_count = 0;

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

static float seq_tempo = 110.0f; // slower, tense tempo
// 16-step tense espionage bassline
static int bass_notes[16] = { 33, 33, 0, 33, 36, 0, 33, 38, 33, 33, 0, 33, 36, 35, 34, 33 };
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

void SynthAudioCallback(void *buffer, unsigned int frames) {
    short *out = (short *)buffer;
    float sample_rate = 44100.0f;
    static float bass_phase = 0.0f;
    static float filter_val = 0.0f;
    
    int samples_per_step = (int)(sample_rate * (60.0f / seq_tempo) / 4.0f);
    
    for (unsigned int i = 0; i < frames; i++) {
        int step = (audio_tick / samples_per_step) % 16;
        int sub_step = audio_tick % samples_per_step;
        audio_tick++;
        
        float bass_out = 0.0f;
        int midi_note = bass_notes[step];
        if (midi_note > 0) {
            float freq = 440.0f * powf(2.0f, (midi_note - 69.0f) / 12.0f);
            bass_phase += freq / sample_rate;
            if (bass_phase >= 1.0f) bass_phase -= 1.0f;
            
            // Triangle wave for smooth, tense bass
            float tri = 0.0f;
            if (bass_phase < 0.5f) tri = 4.0f * bass_phase - 1.0f;
            else tri = 3.0f - 4.0f * bass_phase;
            
            float step_t = (float)sub_step / samples_per_step;
            float env = expf(-8.0f * step_t);
            
            float target_cutoff = 0.03f + 0.08f * env;
            filter_val += (tri - filter_val) * target_cutoff;
            bass_out = filter_val * env * 0.45f;
        }
        
        // Tense heartbeat kick drum
        float kick = 0.0f;
        if (step % 4 == 0 || step % 4 == 1) { // double heartbeat beat
            int kick_sub = (step % 4 == 0) ? sub_step : (sub_step + samples_per_step);
            float kick_t = (float)kick_sub / (samples_per_step * 2.0f);
            if (kick_t < 0.3f) {
                float kick_freq = 100.0f * expf(-20.0f * kick_t) + 35.0f;
                static float kick_phase = 0.0f;
                kick_phase += kick_freq / sample_rate;
                kick = sinf(2.0f * PI * kick_phase) * expf(-8.0f * kick_t) * 0.65f;
            }
        }
        
        // Very soft hi-hat sizzle
        float sizzle = 0.0f;
        if (step % 2 == 1) {
            float hh_t = (float)sub_step / samples_per_step;
            float hh_env = expf(-50.0f * hh_t);
            sizzle = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * hh_env * 0.04f;
        }
        
        float music_mix = bass_out + kick + sizzle;
        
        // SFX mixing
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
        
        out[i] = (short)(final_mix * 10000.0f);
    }
}

/* ─────────────────────────── Helpers ─────────────────────────── */

Texture2D GenerateAssassinTexture() {
    Image img = GenImageColor(64, 64, BLANK);
    // Cloak (dark blue/grey, layered for shading)
    ImageDrawCircle(&img, 32, 32, 24, (Color){15, 15, 22, 255});
    ImageDrawCircle(&img, 32, 32, 20, (Color){28, 28, 38, 255});
    ImageDrawCircle(&img, 32, 32, 16, (Color){40, 40, 55, 255});
    
    // Hood outline (pointing backward)
    ImageDrawTriangle(&img, (Vector2){32, 8}, (Vector2){20, 28}, (Vector2){44, 28}, (Color){15, 15, 22, 255});
    ImageDrawTriangle(&img, (Vector2){32, 12}, (Vector2){24, 28}, (Vector2){40, 28}, (Color){28, 28, 38, 255});

    // Masked face area
    ImageDrawCircle(&img, 32, 28, 9, (Color){10, 10, 14, 255});
    // Glowing cyan/neon visor eyes
    ImageDrawRectangle(&img, 27, 26, 4, 3, (Color){ 0, 240, 255, 255 });
    ImageDrawRectangle(&img, 33, 26, 4, 3, (Color){ 0, 240, 255, 255 });

    // Red sash/belt details
    ImageDrawRectangle(&img, 22, 42, 20, 4, (Color){ 200, 20, 60, 255 });
    
    // Steel dagger held forward (at top right of sprite)
    ImageDrawRectangle(&img, 45, 2, 4, 10, (Color){ 200, 200, 220, 255 }); // blade
    ImageDrawRectangle(&img, 43, 12, 8, 2, (Color){ 80, 80, 80, 255 }); // hilt
    
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

Texture2D GenerateGuardTexture() {
    Image img = GenImageColor(64, 64, BLANK);
    // SWAT body
    ImageDrawCircle(&img, 32, 32, 24, (Color){ 10, 20, 15, 255 }); // shadow
    ImageDrawCircle(&img, 32, 32, 20, (Color){ 30, 42, 36, 255 }); // vest
    
    // Helmet
    ImageDrawCircle(&img, 32, 28, 11, (Color){ 15, 20, 25, 255 });
    ImageDrawCircle(&img, 32, 28, 9, (Color){ 35, 45, 55, 255 });
    
    // Visor/Goggles glow line
    ImageDrawRectangle(&img, 24, 26, 16, 3, (Color){ 50, 255, 100, 255 });
    
    // Rifle held in hands, pointing forward (right hand)
    ImageDrawRectangle(&img, 44, 2, 6, 24, (Color){ 10, 10, 10, 255 }); // rifle
    ImageDrawRectangle(&img, 42, 16, 4, 10, (Color){ 40, 40, 45, 255 });
    
    // Heavy shoulder pads
    ImageDrawCircle(&img, 14, 32, 5, (Color){ 20, 32, 26, 255 });
    ImageDrawCircle(&img, 50, 32, 5, (Color){ 20, 32, 26, 255 });
    
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

bool CheckWallCollision(Vector2 pos, float radius, int levelIdx) {
    int startX = (pos.x - radius) / TILE_SIZE;
    int endX = (pos.x + radius) / TILE_SIZE;
    int startY = (pos.y - radius) / TILE_SIZE;
    int endY = (pos.y + radius) / TILE_SIZE;

    for (int y = startY; y <= endY; y++) {
        for (int x = startX; x <= endX; x++) {
            if (x < 0 || x >= MAP_WIDTH || y < 0 || y >= MAP_HEIGHT) continue;
            if (levels[levelIdx].tiles[y][x] == 1) {
                Rectangle rect = { (float)x * TILE_SIZE, (float)y * TILE_SIZE, TILE_SIZE, TILE_SIZE };
                if (CheckCollisionCircleRec(pos, radius, rect)) return true;
            }
        }
    }
    return false;
}

bool IsPointVisible(Vector2 viewer, Vector2 target, int levelIdx) {
    float dist = Vector2Distance(viewer, target);
    Vector2 dir = Vector2Normalize(Vector2Subtract(target, viewer));
    
    // Check if ray passes through smoke clouds
    for (int i = 0; i < MAX_SMOKES; i++) {
        if (smoke_clouds[i].active) {
            float step = 10.0f;
            for (float d = 0; d < dist; d += step) {
                Vector2 p = Vector2Add(viewer, Vector2Scale(dir, d));
                if (Vector2Distance(p, smoke_clouds[i].pos) < smoke_clouds[i].radius * 0.85f) {
                    return false; // smoke blocks vision
                }
            }
        }
    }

    // Check if ray hits walls
    float stepSize = 4.0f;
    for (float d = stepSize; d < dist; d += stepSize) {
        Vector2 p = Vector2Add(viewer, Vector2Scale(dir, d));
        int tx = p.x / TILE_SIZE;
        int ty = p.y / TILE_SIZE;
        if (tx >= 0 && tx < MAP_WIDTH && ty >= 0 && ty < MAP_HEIGHT) {
            if (levels[levelIdx].tiles[ty][tx] == 1) return false;
        }
    }
    return true;
}

static void AddBloodPool(Vector2 pos) {
    if (blood_pool_count >= MAX_BLOOD_POOLS) {
        for (int i = 0; i < MAX_BLOOD_POOLS - 1; i++) {
            blood_pools[i] = blood_pools[i + 1];
        }
        blood_pool_count = MAX_BLOOD_POOLS - 1;
    }
    BloodPool *bp = &blood_pools[blood_pool_count++];
    bp->pos = pos;
    bp->size = (float)GetRandomValue(12, 24);
    bp->color = (Color){ (unsigned char)GetRandomValue(130, 170), 0, 0, 180 };
}

static void SpawnGhost(Vector2 pos) {
    if (ghost_count >= MAX_GHOSTS) {
        for (int i = 0; i < MAX_GHOSTS - 1; i++) {
            dash_ghosts[i] = dash_ghosts[i + 1];
        }
        ghost_count = MAX_GHOSTS - 1;
    }
    dash_ghosts[ghost_count++] = (DashGhost){ pos, 0.6f };
}

static void AddParticleEx(Vector2 pos, Color col, float speed, float size, ParticleType type, float custom_life, int count) {
    for (int i = 0; i < count; i++) {
        if (particle_count >= MAX_PARTICLES) return;
        Particle *p = &particles[particle_count++];
        float angle = (float)GetRandomValue(0, 360) * DEG2RAD;
        float vel_len = speed * (0.3f + 0.7f * (float)GetRandomValue(0, 100) / 100.0f);
        p->pos = pos;
        p->vel = (Vector2){ cosf(angle) * vel_len, sinf(angle) * vel_len };
        p->color = col;
        p->max_life = (custom_life > 0.0f ? custom_life : (0.2f + 0.3f * (float)GetRandomValue(0, 100) / 100.0f));
        p->life = p->max_life;
        p->size = size * (0.6f + 0.5f * (float)GetRandomValue(0, 100) / 100.0f);
        p->type = type;
    }
}

static void UpdateParticles(float dt) {
    for (int i = 0; i < particle_count; ) {
        Particle *p = &particles[i];
        p->life -= dt;
        p->pos.x += p->vel.x * dt;
        p->pos.y += p->vel.y * dt;
        p->vel.x *= 0.94f; // friction
        p->vel.y *= 0.94f;

        if (p->type == PARTICLE_SMOKE) {
            p->size += dt * 8.0f; // expand smoke
        }

        if (p->life <= 0) {
            particles[i] = particles[--particle_count];
        } else {
            i++;
        }
    }
}

static void DrawParticles() {
    for (int i = 0; i < particle_count; i++) {
        Particle *p = &particles[i];
        float alpha = p->life / p->max_life;
        Color c = p->color;
        c.a = (unsigned char)(alpha * p->color.a);
        DrawCircle((int)p->pos.x, (int)p->pos.y, p->size, c);
    }
}

static void TriggerSmoke(Vector2 pos) {
    for (int i = 0; i < MAX_SMOKES; i++) {
        if (!smoke_clouds[i].active) {
            smoke_clouds[i].pos = pos;
            smoke_clouds[i].radius = 120.0f;
            smoke_clouds[i].life = 6.0f;
            smoke_clouds[i].active = true;
            PlaySynthSFX(100.0f, 50.0f, 0.4f, 0.6f, true); // smoke burst sfx
            return;
        }
    }
}

/* ─────────────────────────── Level Data ─────────────────────────── */

void InitLevels() {
    // Level 1
    memset(&levels[0], 0, sizeof(Level));
    for (int x = 0; x < MAP_WIDTH; x++) { levels[0].tiles[0][x] = 1; levels[0].tiles[MAP_HEIGHT-1][x] = 1; }
    for (int y = 0; y < MAP_HEIGHT; y++) { levels[0].tiles[y][0] = 1; levels[0].tiles[y][MAP_WIDTH-1] = 1; }
    for (int x = 4; x < 12; x++) levels[0].tiles[6][x] = 1;
    for (int y = 8; y < 14; y++) levels[0].tiles[y][15] = 1;
    levels[0].playerSpawn = (Vector2){ 100, 100 };
    levels[0].enemyCount = 2;
    levels[0].enemies[0] = (Enemy){ .base = {{500, 300}, 0, true}, .waypoints = {{500, 300}, {800, 300}}, .numWaypoints = 2 };
    levels[0].enemies[1] = (Enemy){ .base = {{200, 600}, 0, true}, .waypoints = {{200, 600}, {200, 200}}, .numWaypoints = 2 };

    // Level 2
    memset(&levels[1], 0, sizeof(Level));
    for (int x = 0; x < MAP_WIDTH; x++) { levels[1].tiles[0][x] = 1; levels[1].tiles[MAP_HEIGHT-1][x] = 1; }
    for (int y = 0; y < MAP_HEIGHT; y++) { levels[1].tiles[y][0] = 1; levels[1].tiles[y][MAP_WIDTH-1] = 1; }
    for (int i = 4; i < 12; i++) { levels[1].tiles[i][7] = 1; levels[1].tiles[8][i+5] = 1; }
    levels[1].playerSpawn = (Vector2){ 80, 80 };
    levels[1].enemyCount = 3;
    levels[1].enemies[0] = (Enemy){ .base = {{400, 150}, 0, true}, .waypoints = {{400, 150}, {800, 150}}, .numWaypoints = 2 };
    levels[1].enemies[1] = (Enemy){ .base = {{800, 400}, 0, true}, .waypoints = {{800, 400}, {800, 650}}, .numWaypoints = 2 };
    levels[1].enemies[2] = (Enemy){ .base = {{400, 600}, 0, true}, .waypoints = {{400, 600}, {200, 600}}, .numWaypoints = 2 };
    
    // Level 3
    memset(&levels[2], 0, sizeof(Level));
    for (int x = 0; x < MAP_WIDTH; x++) { levels[2].tiles[0][x] = 1; levels[2].tiles[MAP_HEIGHT-1][x] = 1; }
    for (int y = 0; y < MAP_HEIGHT; y++) { levels[2].tiles[y][0] = 1; levels[2].tiles[y][MAP_WIDTH-1] = 1; }
    for (int i = 5; i < 16; i+=2) { levels[2].tiles[i][5] = 1; levels[2].tiles[i][15] = 1; }
    levels[2].playerSpawn = (Vector2){ 512, 384 };
    levels[2].enemyCount = 4;
    levels[2].enemies[0] = (Enemy){ .base = {{100, 100}, 0, true}, .waypoints = {{100, 100}, {900, 100}}, .numWaypoints = 2 };
    levels[2].enemies[1] = (Enemy){ .base = {{900, 668}, 0, true}, .waypoints = {{900, 668}, {100, 668}}, .numWaypoints = 2 };
    levels[2].enemies[2] = (Enemy){ .base = {{100, 668}, 0, true}, .waypoints = {{100, 668}, {100, 100}}, .numWaypoints = 2 };
    levels[2].enemies[3] = (Enemy){ .base = {{900, 100}, 0, true}, .waypoints = {{900, 100}, {900, 668}}, .numWaypoints = 2 };
}

/* ─────────────────────────── Main Logic ─────────────────────────── */

int main() {
    InitWindow(SCREEN_W, SCREEN_H, "Hunter Neon Assassin");
    SetTargetFPS(60);

    texAssassin = GenerateAssassinTexture();
    texGuard = GenerateGuardTexture();
    
    // Initialize Audio
    InitAudioDevice();
    AudioStream stream = LoadAudioStream(44100, 16, 1);
    SetAudioStreamCallback(stream, SynthAudioCallback);
    PlayAudioStream(stream);

    InitLevels();
    
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        float t = (float)GetTime();
        if (dt > 0.05f) dt = 0.05f;

        if (screenShake > 0) screenShake -= dt * 2.0f;
        if (screenShake < 0) screenShake = 0;

        // Global Escape key back to Menu
        if (IsKeyPressed(KEY_ESCAPE) && currentScreen == SCREEN_PLAY) {
            currentScreen = SCREEN_MENU;
        }

        // Update
        switch (currentScreen) {
            case SCREEN_MENU:
                if (IsKeyPressed(KEY_ENTER)) {
                    InitLevels(); // Reset everything
                    currentLevelIdx = 0;
                    player.base.pos = levels[currentLevelIdx].playerSpawn;
                    player.base.alive = true;
                    player.speed = 240.0f;
                    player.health = 100.0f;
                    player.max_health = 100.0f;
                    player.smoke_bombs = 3;
                    player.dash_cooldown = 0.0f;
                    player.dash_timer = 0.0f;
                    player.slash_timer = 0.0f;

                    blood_pool_count = 0;
                    ghost_count = 0;
                    particle_count = 0;
                    memset(smoke_clouds, 0, sizeof(smoke_clouds));
                    lure_wave.active = false;
                    pebble.active = false;

                    currentScreen = SCREEN_PLAY;
                }
                break;
            case SCREEN_PLAY: {
                Vector2 move = {0};
                if (IsKeyDown(KEY_W)) move.y -= 1;
                if (IsKeyDown(KEY_S)) move.y += 1;
                if (IsKeyDown(KEY_A)) move.x -= 1;
                if (IsKeyDown(KEY_D)) move.x += 1;
                
                // Dash cooldown decay
                if (player.dash_cooldown > 0) player.dash_cooldown -= dt;
                if (player.slash_timer > 0) player.slash_timer -= dt;

                // Dash triggering
                if ((IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_LEFT_SHIFT)) && player.dash_cooldown <= 0 && Vector2Length(move) > 0) {
                    player.dash_timer = 0.15f;
                    player.dash_cooldown = 1.0f;
                    player.dash_dir = Vector2Normalize(move);
                    PlaySynthSFX(150.0f, 400.0f, 0.15f, 0.35f, true); // whoosh
                }

                // Dash Movement
                if (player.dash_timer > 0) {
                    player.dash_timer -= dt;
                    float dashSpeed = player.speed * 2.5f;
                    Vector2 nextX = { player.base.pos.x + player.dash_dir.x * dashSpeed * dt, player.base.pos.y };
                    if (!CheckWallCollision(nextX, PLAYER_RADIUS, currentLevelIdx)) player.base.pos.x = nextX.x;
                    Vector2 nextY = { player.base.pos.x, player.base.pos.y + player.dash_dir.y * dashSpeed * dt };
                    if (!CheckWallCollision(nextY, PLAYER_RADIUS, currentLevelIdx)) player.base.pos.y = nextY.y;
                    
                    SpawnGhost(player.base.pos);
                } else if (Vector2Length(move) > 0) {
                    // Regular Movement
                    move = Vector2Scale(Vector2Normalize(move), player.speed * dt);
                    Vector2 nextX = { player.base.pos.x + move.x, player.base.pos.y };
                    if (!CheckWallCollision(nextX, PLAYER_RADIUS, currentLevelIdx)) player.base.pos.x = nextX.x;
                    Vector2 nextY = { player.base.pos.x, player.base.pos.y + move.y };
                    if (!CheckWallCollision(nextY, PLAYER_RADIUS, currentLevelIdx)) player.base.pos.y = nextY.y;
                    
                    player.base.rotation = atan2f(move.y, move.x) * RAD2DEG + 90;
                }

                // Smoke bomb trigger
                if (IsKeyPressed(KEY_Q) && player.smoke_bombs > 0) {
                    player.smoke_bombs--;
                    TriggerSmoke(player.base.pos);
                }

                // Pebble lure launch
                if (IsKeyPressed(KEY_F) && !pebble.active) {
                    Vector2 mPos = GetMousePosition();
                    if (Vector2Distance(player.base.pos, mPos) < 300.0f) {
                        pebble.start = player.base.pos;
                        pebble.pos = player.base.pos;
                        pebble.target = mPos;
                        pebble.t = 0.0f;
                        pebble.active = true;
                        PlaySynthSFX(600.0f, 200.0f, 0.15f, 0.4f, false);
                    }
                }

                // Pebble flight updating
                if (pebble.active) {
                    pebble.t += dt * 3.0f;
                    if (pebble.t >= 1.0f) {
                        pebble.active = false;
                        
                        // Lure trigger
                        lure_wave.pos = pebble.target;
                        lure_wave.radius = 0.0f;
                        lure_wave.maxRadius = 180.0f;
                        lure_wave.life = 0.8f;
                        lure_wave.active = true;
                        
                        PlaySynthSFX(500.0f, 400.0f, 0.08f, 0.35f, false);
                        
                        // Alert guards to investigate noise location
                        for (int k = 0; k < levels[currentLevelIdx].enemyCount; k++) {
                            Enemy *e = &levels[currentLevelIdx].enemies[k];
                            if (e->base.alive) {
                                float dToLure = Vector2Distance(e->base.pos, lure_wave.pos);
                                if (dToLure < 220.0f) {
                                    e->state = STATE_INVESTIGATE;
                                    e->targetPos = lure_wave.pos;
                                    e->waitTime = 2.0f;
                                    e->waitTimer = 0.0f;
                                    
                                    PlaySynthSFX(400.0f, 500.0f, 0.08f, 0.2f, false);
                                }
                            }
                        }
                    } else {
                        pebble.pos = Vector2Lerp(pebble.start, pebble.target, pebble.t);
                        float height = sinf(pebble.t * PI) * 50.0f;
                        pebble.pos.y -= height;
                    }
                }

                // Lure rings updating
                if (lure_wave.active) {
                    lure_wave.radius += dt * 250.0f;
                    lure_wave.life -= dt;
                    if (lure_wave.life <= 0) {
                        lure_wave.active = false;
                    }
                }

                // Smoke clouds decay and particle emission
                for (int s = 0; s < MAX_SMOKES; s++) {
                    if (smoke_clouds[s].active) {
                        smoke_clouds[s].life -= dt;
                        
                        if (GetRandomValue(0, 5) == 0) {
                            Vector2 offset = { (float)GetRandomValue(-50, 50), (float)GetRandomValue(-50, 50) };
                            Vector2 p_pos = Vector2Add(smoke_clouds[s].pos, offset);
                            AddParticleEx(p_pos, (Color){ 100, 100, 110, 100 }, 10.0f, (float)GetRandomValue(20, 40), PARTICLE_SMOKE, 1.2f, 1);
                        }
                        
                        if (smoke_clouds[s].life <= 0) {
                            smoke_clouds[s].active = false;
                        }
                    }
                }

                // Dash ghost decay
                for (int i = 0; i < ghost_count; i++) {
                    dash_ghosts[i].alpha -= dt * 3.5f;
                    if (dash_ghosts[i].alpha <= 0) {
                        for (int j = i; j < ghost_count - 1; j++) {
                            dash_ghosts[j] = dash_ghosts[j + 1];
                        }
                        ghost_count--;
                        i--;
                    }
                }

                // Particles updating
                UpdateParticles(dt);
                
                // Update guards & AI state machine
                bool anyAlive = false;
                for (int i = 0; i < levels[currentLevelIdx].enemyCount; i++) {
                    Enemy *e = &levels[currentLevelIdx].enemies[i];
                    if (!e->base.alive) continue;
                    anyAlive = true;
                    
                    if (e->muzzle_flash_timer > 0) e->muzzle_flash_timer -= dt;

                    // AI states transitions
                    if (e->state == STATE_CHASE) {
                        e->targetPos = player.base.pos;
                        
                        // Check if player is lost (no LOS)
                        if (!IsPointVisible(e->base.pos, player.base.pos, currentLevelIdx)) {
                            e->chaseTimer -= dt;
                            if (e->chaseTimer <= 0) {
                                e->state = STATE_INVESTIGATE;
                                e->targetPos = player.base.pos; // search last known position
                                e->waitTime = 2.0f;
                                e->waitTimer = 0.0f;
                            }
                        } else {
                            e->chaseTimer = 4.0f; // refresh
                            
                            // Shoot player within distance
                            float distToPlayer = Vector2Distance(e->base.pos, player.base.pos);
                            if (distToPlayer < 250.0f) {
                                e->shoot_timer += dt;
                                if (e->shoot_timer >= 0.8f) {
                                    e->shoot_timer = 0.0f;
                                    e->muzzle_flash_timer = 0.08f;
                                    player.health -= 20.0f;
                                    screenShake = 0.25f;
                                    
                                    PlaySynthSFX(400.0f, 100.0f, 0.1f, 0.5f, true);
                                    
                                    // blood splatters
                                    AddParticleEx(player.base.pos, (Color){ 200, 20, 20, 255 }, 120.0f, 3.5f, PARTICLE_SPARK, 0.4f, 8);
                                }
                            }
                        }
                    }

                    // Move AI based on current active state
                    if (e->state == STATE_CHASE) {
                        Vector2 toTarget = Vector2Subtract(e->targetPos, e->base.pos);
                        float distToTarget = Vector2Length(toTarget);
                        if (distToTarget > 15.0f) {
                            Vector2 eMove = Vector2Scale(Vector2Normalize(toTarget), 160.0f * dt);
                            e->base.pos = Vector2Add(e->base.pos, eMove);
                            e->base.rotation = atan2f(eMove.y, eMove.x) * RAD2DEG + 90;
                        }
                    } else if (e->state == STATE_INVESTIGATE) {
                        Vector2 toTarget = Vector2Subtract(e->targetPos, e->base.pos);
                        float distToTarget = Vector2Length(toTarget);
                        if (distToTarget > 10.0f) {
                            Vector2 eMove = Vector2Scale(Vector2Normalize(toTarget), 110.0f * dt);
                            e->base.pos = Vector2Add(e->base.pos, eMove);
                            e->base.rotation = atan2f(eMove.y, eMove.x) * RAD2DEG + 90;
                        } else {
                            // Arrived at destination, sweep visor side-to-side
                            e->waitTimer += dt;
                            e->base.rotation += sinf(e->waitTimer * 8.0f) * 60.0f * dt;
                            if (e->waitTimer >= e->waitTime) {
                                e->state = STATE_PATROL;
                            }
                        }
                    } else {
                        // STATE_PATROL
                        if (e->numWaypoints > 0) {
                            Vector2 target = e->waypoints[e->currentWaypoint];
                            Vector2 toTarget = Vector2Subtract(target, e->base.pos);
                            if (Vector2Length(toTarget) > 5.0f) {
                                Vector2 eMove = Vector2Scale(Vector2Normalize(toTarget), 90.0f * dt);
                                e->base.pos = Vector2Add(e->base.pos, eMove);
                                e->base.rotation = atan2f(eMove.y, eMove.x) * RAD2DEG + 90;
                            } else {
                                e->currentWaypoint = (e->currentWaypoint + 1) % e->numWaypoints;
                            }
                        }
                    }

                    // Detection line-of-sight sweep
                    Vector2 toPlayer = Vector2Subtract(player.base.pos, e->base.pos);
                    float dist = Vector2Length(toPlayer);
                    if (dist < VISION_DISTANCE) {
                        float angleToPlayer = atan2f(toPlayer.y, toPlayer.x) * RAD2DEG + 90;
                        float diff = fmodf(fabsf(angleToPlayer - e->base.rotation) + 180, 360) - 180;
                        if (fabsf(diff) < VISION_ANGLE/2.0f) {
                            if (IsPointVisible(e->base.pos, player.base.pos, currentLevelIdx)) {
                                if (e->state != STATE_CHASE) {
                                    e->state = STATE_CHASE;
                                    e->chaseTimer = 4.0f;
                                    
                                    PlaySynthSFX(700.0f, 900.0f, 0.15f, 0.4f, false);
                                    
                                    // Alert all other guards in area
                                    for (int n = 0; n < levels[currentLevelIdx].enemyCount; n++) {
                                        Enemy *other = &levels[currentLevelIdx].enemies[n];
                                        if (other->base.alive && other->state != STATE_CHASE) {
                                            other->state = STATE_CHASE;
                                            other->targetPos = player.base.pos;
                                            other->chaseTimer = 4.0f;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    // Silent assassination execution
                    if (CheckCollisionCircles(player.base.pos, PLAYER_RADIUS + 4.0f, e->base.pos, ENEMY_RADIUS)) {
                        e->base.alive = false;
                        screenShake = 0.35f;
                        player.slash_timer = 0.2f;
                        
                        AddBloodPool(e->base.pos);
                        AddParticleEx(e->base.pos, (Color){ 200, 10, 20, 255 }, 180.0f, 4.0f, PARTICLE_SPARK, 0.6f, 20);
                        
                        PlaySynthSFX(800.0f, 200.0f, 0.18f, 0.5f, false); // Slash
                        PlaySynthSFX(150.0f, 70.0f, 0.3f, 0.45f, true); // groan
                    }
                }
                
                // Death check
                if (player.health <= 0) {
                    player.health = 0;
                    player.base.alive = false;
                    currentScreen = SCREEN_GAME_OVER;
                }

                if (!anyAlive) {
                    if (currentLevelIdx < MAX_LEVELS - 1) currentScreen = SCREEN_WIN_LEVEL;
                    else currentScreen = SCREEN_WIN_GAME;
                }
            } break;
            case SCREEN_WIN_LEVEL:
                if (IsKeyPressed(KEY_ENTER)) {
                    currentLevelIdx++;
                    player.base.pos = levels[currentLevelIdx].playerSpawn;
                    
                    player.smoke_bombs = 3;
                    player.dash_cooldown = 0.0f;
                    player.dash_timer = 0.0f;
                    player.slash_timer = 0.0f;

                    blood_pool_count = 0;
                    ghost_count = 0;
                    particle_count = 0;
                    memset(smoke_clouds, 0, sizeof(smoke_clouds));
                    lure_wave.active = false;
                    pebble.active = false;

                    currentScreen = SCREEN_PLAY;
                }
                break;
            case SCREEN_GAME_OVER:
            case SCREEN_WIN_GAME:
                if (IsKeyPressed(KEY_ENTER)) currentScreen = SCREEN_MENU;
                break;
        }

        // Draw
        BeginDrawing();
        ClearBackground((Color){10, 10, 15, 255});
        
        Vector2 shakeVec = { (float)GetRandomValue(-10, 10) * screenShake, (float)GetRandomValue(-10, 10) * screenShake };
        
        switch (currentScreen) {
            case SCREEN_MENU: {
                ClearBackground(COL_SKY);
                float glow = sinf(t * 5.0f) * 4.0f;
                DrawText("HUNTER NEON ASSASSIN", SCREEN_W/2 - MeasureText("HUNTER NEON ASSASSIN", 36)/2 + (int)glow/2, 120, 36, COL_NEON_PINK);
                DrawText("HUNTER NEON ASSASSIN", SCREEN_W/2 - MeasureText("HUNTER NEON ASSASSIN", 36)/2, 120, 36, COL_NEON_BLUE);

                DrawText("A TENSE ESPIONAGE STEALTH GAME", SCREEN_W/2 - MeasureText("A TENSE ESPIONAGE STEALTH GAME", 16)/2, 170, 16, COL_CYBER_PURPLE);

                // Start button
                DrawRectangle(SCREEN_W/2 - 120, 230, 240, 45, (Color){ 30, 20, 50, 250 });
                DrawRectangleLines(SCREEN_W/2 - 120, 230, 240, 45, COL_NEON_GREEN);
                DrawText("PRESS ENTER TO PLAY", SCREEN_W/2 - MeasureText("PRESS ENTER TO PLAY", 14)/2, 245, 14, COL_NEON_GREEN);

                // Controls Box
                DrawRectangle(SCREEN_W/2 - 250, 310, 500, 220, (Color){ 20, 15, 30, 200 });
                DrawRectangleLines(SCREEN_W/2 - 250, 310, 500, 220, COL_CYBER_PURPLE);

                DrawText("CONTROLS:", SCREEN_W/2 - 230, 325, 14, COL_NEON_BLUE);
                DrawText("- WASD Keys: Stealth Movement", SCREEN_W/2 - 230, 350, 14, WHITE);
                DrawText("- SPACE / L-SHIFT: Dash (Fast movement with ghosts)", SCREEN_W/2 - 230, 372, 14, WHITE);
                DrawText("- Q Key: Smoke Bomb (Blocks guard vision for 6s)", SCREEN_W/2 - 230, 394, 14, WHITE);
                DrawText("- F Key + Mouse: Throw Pebble Lure (Lure guards)", SCREEN_W/2 - 230, 416, 14, WHITE);
                DrawText("- Melee Strike: Collide with guards from behind to execute", SCREEN_W/2 - 230, 438, 14, WHITE);
                DrawText("- Escape from Spot: Break line of sight for 4s to escape", SCREEN_W/2 - 230, 460, 14, WHITE);
                DrawText("- ESC: Exit to Menu", SCREEN_W/2 - 230, 482, 14, WHITE);
                
                // Scanlines overlay for menu
                for (int y = 0; y < SCREEN_H; y += 3) {
                    DrawLine(0, y, SCREEN_W, y, (Color){ 0, 0, 0, 20 });
                }
                break;
            }
            case SCREEN_PLAY: {
                // Apply shake only to world rendering
                BeginMode2D((Camera2D){ .offset = shakeVec, .target = (Vector2){0, 0}, .rotation = 0, .zoom = 1.0f });
                
                // Draw Map Tiles
                for (int y = 0; y < MAP_HEIGHT; y++) {
                    for (int x = 0; x < MAP_WIDTH; x++) {
                        if (levels[currentLevelIdx].tiles[y][x] == 1) {
                            // High-tech walls
                            DrawRectangle(x * TILE_SIZE, y * TILE_SIZE, TILE_SIZE, TILE_SIZE, (Color){35, 30, 50, 255});
                            DrawRectangleLines(x * TILE_SIZE + 2, y * TILE_SIZE + 2, TILE_SIZE - 4, TILE_SIZE - 4, (Color){ 120, 60, 180, 255 });
                            DrawLine(x * TILE_SIZE, y * TILE_SIZE, x * TILE_SIZE + TILE_SIZE, y * TILE_SIZE + TILE_SIZE, (Color){ 255, 255, 255, 15 });
                        } else {
                            // Dark floor grids
                            DrawRectangle(x * TILE_SIZE, y * TILE_SIZE, TILE_SIZE, TILE_SIZE, (Color){15, 12, 24, 255});
                            DrawRectangleLines(x * TILE_SIZE, y * TILE_SIZE, TILE_SIZE, TILE_SIZE, (Color){ 20, 16, 32, 255 });
                        }
                    }
                }
                
                // Draw blood pools
                for (int i = 0; i < blood_pool_count; i++) {
                    DrawCircle(blood_pools[i].pos.x, blood_pools[i].pos.y, blood_pools[i].size, blood_pools[i].color);
                    DrawCircle(blood_pools[i].pos.x + blood_pools[i].size * 0.4f, blood_pools[i].pos.y - blood_pools[i].size * 0.3f, blood_pools[i].size * 0.3f, blood_pools[i].color);
                }

                // Draw dash ghosts
                for (int i = 0; i < ghost_count; i++) {
                    DrawCircle(dash_ghosts[i].pos.x, dash_ghosts[i].pos.y, PLAYER_RADIUS, (Color){ 0, 150, 255, (unsigned char)(255 * dash_ghosts[i].alpha) });
                }

                // Draw smoke clouds
                for (int s = 0; s < MAX_SMOKES; s++) {
                    if (smoke_clouds[s].active) {
                        DrawCircle(smoke_clouds[s].pos.x, smoke_clouds[s].pos.y, smoke_clouds[s].radius, (Color){ 80, 80, 90, 40 });
                        DrawCircleLines(smoke_clouds[s].pos.x, smoke_clouds[s].pos.y, smoke_clouds[s].radius, (Color){ 120, 120, 130, 80 });
                    }
                }

                // Draw Pebble Lure
                if (pebble.active) {
                    DrawCircle(pebble.pos.x, pebble.pos.y, 4, LIGHTGRAY);
                    DrawCircleLines(pebble.pos.x, pebble.pos.y, 4, BLACK);
                }

                // Draw Lure sound wave rings
                if (lure_wave.active) {
                    float pct = lure_wave.life / 0.8f;
                    Color wave_col = (Color){ 0, 220, 255, (unsigned char)(pct * 120) };
                    DrawCircleLines(lure_wave.pos.x, lure_wave.pos.y, lure_wave.radius, wave_col);
                    DrawCircleLines(lure_wave.pos.x, lure_wave.pos.y, lure_wave.radius * 0.7f, wave_col);
                }

                int aliveCount = 0;
                // Draw Enemies & Volumetric flashlight cone vision
                for (int i = 0; i < levels[currentLevelIdx].enemyCount; i++) {
                    Enemy *e = &levels[currentLevelIdx].enemies[i];
                    if (!e->base.alive) continue;
                    aliveCount++;
                    
                    // Volumetric Flashlight Cone
                    Color cone_color = (Color){ 255, 240, 120, 30 };
                    if (e->state == STATE_INVESTIGATE) cone_color = (Color){ 255, 150, 20, 45 };
                    else if (e->state == STATE_CHASE) cone_color = (Color){ 255, 30, 30, (unsigned char)(40 + sinf(t * 20.0f) * 20) };
                    
                    DrawCircleSector(e->base.pos, VISION_DISTANCE, e->base.rotation - 90.0f - VISION_ANGLE/2.0f, e->base.rotation - 90.0f + VISION_ANGLE/2.0f, 20, cone_color);
                    
                    // Draw outer border lines for flashlight beam
                    float rad1 = (e->base.rotation - 90.0f - VISION_ANGLE/2.0f) * DEG2RAD;
                    float rad2 = (e->base.rotation - 90.0f + VISION_ANGLE/2.0f) * DEG2RAD;
                    Vector2 beam_end1 = Vector2Add(e->base.pos, (Vector2){ cosf(rad1) * VISION_DISTANCE, sinf(rad1) * VISION_DISTANCE });
                    Vector2 beam_end2 = Vector2Add(e->base.pos, (Vector2){ cosf(rad2) * VISION_DISTANCE, sinf(rad2) * VISION_DISTANCE });
                    cone_color.a = 70;
                    DrawLineV(e->base.pos, beam_end1, cone_color);
                    DrawLineV(e->base.pos, beam_end2, cone_color);

                    // Muzzle flash line if guard is shooting
                    if (e->muzzle_flash_timer > 0) {
                        DrawLineEx(e->base.pos, player.base.pos, 3.0f, (Color){ 255, 200, 50, 255 });
                        Vector2 gun_pos = Vector2Add(e->base.pos, (Vector2){ cosf(e->base.rotation * DEG2RAD - PI/2) * 16, sinf(e->base.rotation * DEG2RAD - PI/2) * 16 });
                        DrawCircle(gun_pos.x, gun_pos.y, 8, (Color){ 255, 150, 50, 200 });
                    }
                    
                    // Hit flash / regular draw
                    DrawTexturePro(texGuard, (Rectangle){0, 0, 64, 64}, (Rectangle){e->base.pos.x, e->base.pos.y, 48, 48}, (Vector2){24, 24}, e->base.rotation, WHITE);
                    
                    // Exclamation warning / question mark markers
                    if (e->state == STATE_CHASE) {
                        float bob = sinf(t * 12.0f) * 4.0f;
                        DrawText("!", e->base.pos.x - 4, e->base.pos.y - ENEMY_RADIUS - 22 + bob, 20, RED);
                    } else if (e->state == STATE_INVESTIGATE) {
                        float bob = sinf(t * 10.0f) * 3.0f;
                        DrawText("?", e->base.pos.x - 4, e->base.pos.y - ENEMY_RADIUS - 22 + bob, 20, ORANGE);
                    }
                }
                
                // Draw Player
                DrawTexturePro(texAssassin, (Rectangle){0, 0, 64, 64}, (Rectangle){player.base.pos.x, player.base.pos.y, 48, 48}, (Vector2){24, 24}, player.base.rotation, WHITE);
                
                // Sword slash arc when attacking
                if (player.slash_timer > 0) {
                    float angle = player.base.rotation - 90.0f;
                    Vector2 slash_center = Vector2Add(player.base.pos, (Vector2){ cosf(angle * DEG2RAD) * 15, sinf(angle * DEG2RAD) * 15 });
                    DrawCircleSector(slash_center, 36.0f, angle - 50.0f, angle + 50.0f, 10, (Color){ 200, 240, 255, (unsigned char)(player.slash_timer / 0.2f * 180) });
                }

                // Draw blood and smoke particles
                DrawParticles();

                EndMode2D();

                // Lure pebble targeting reticle
                Vector2 mPos = GetMousePosition();
                float distToMouse = Vector2Distance(player.base.pos, mPos);
                if (distToMouse < 300.0f) {
                    DrawCircleLines(mPos.x, mPos.y, 8, COL_NEON_PINK);
                    DrawLine(mPos.x - 12, mPos.y, mPos.x + 12, mPos.y, COL_NEON_PINK);
                    DrawLine(mPos.x, mPos.y - 12, mPos.x, mPos.y + 12, COL_NEON_PINK);
                } else {
                    DrawCircleLines(mPos.x, mPos.y, 8, GRAY);
                }
                
                // Glass panel HUD
                DrawRectangle(10, 10, 240, 130, (Color){ 16, 12, 28, 200 });
                DrawRectangleLines(10, 10, 240, 130, COL_CYBER_PURPLE);
                DrawRectangle(12, 12, 236, 2, (Color){ 255, 255, 255, 60 });

                DrawText(TextFormat("LEVEL %d", currentLevelIdx + 1), 20, 20, 18, RAYWHITE);
                DrawText(TextFormat("ENEMIES LEFT: %d", aliveCount), 20, 45, 14, GREEN);
                
                // Smoke Bombs remaining
                DrawText(TextFormat("SMOKES (Q): %d", player.smoke_bombs), 20, 70, 14, COL_NEON_BLUE);
                
                // Pebble Lure status
                DrawText("LURE (F): READY", 20, 92, 14, lure_wave.active ? GRAY : COL_NEON_PINK);
                
                // Health bar
                DrawRectangle(20, 115, 200, 12, (Color){ 60, 20, 40, 180 });
                DrawRectangle(20, 115, (int)(200 * (player.health / player.max_health)), 12, RED);
                DrawRectangleLines(20, 115, 200, 12, WHITE);
            } break;
            case SCREEN_WIN_LEVEL:
                DrawText("LEVEL COMPLETE!", SCREEN_W/2 - 120, SCREEN_H/2 - 20, 30, YELLOW);
                DrawText("PRESS ENTER FOR NEXT LEVEL", SCREEN_W/2 - 180, SCREEN_H/2 + 40, 20, RAYWHITE);
                break;
            case SCREEN_GAME_OVER:
                ClearBackground((Color){ 20, 5, 5, 255 });
                DrawText("SPOTTED & ELIMINATED!", SCREEN_W/2 - MeasureText("SPOTTED & ELIMINATED!", 30)/2, SCREEN_H/2 - 40, 30, RED);
                DrawText("THE GUARDS HAVE OUTSMARTED YOU.", SCREEN_W/2 - MeasureText("THE GUARDS HAVE OUTSMARTED YOU.", 16)/2, SCREEN_H/2 + 10, 16, GRAY);
                DrawText("PRESS ENTER TO RETURN TO MENU", SCREEN_W/2 - MeasureText("PRESS ENTER TO RETURN TO MENU", 16)/2, SCREEN_H/2 + 60, 16, RAYWHITE);
                break;
            case SCREEN_WIN_GAME:
                ClearBackground((Color){ 5, 20, 10, 255 });
                DrawText("MISSION ACCOMPLISHED!", SCREEN_W/2 - MeasureText("MISSION ACCOMPLISHED!", 30)/2, SCREEN_H/2 - 40, 30, GOLD);
                DrawText("ALL GUARDS WERE SILENTLY NEUTRALIZED.", SCREEN_W/2 - MeasureText("ALL GUARDS WERE SILENTLY NEUTRALIZED.", 16)/2, SCREEN_H/2 + 10, 16, GRAY);
                DrawText("PRESS ENTER TO RETURN TO MENU", SCREEN_W/2 - MeasureText("PRESS ENTER TO RETURN TO MENU", 16)/2, SCREEN_H/2 + 60, 16, RAYWHITE);
                break;
        }

        // CRT Scanlines Overlay
        for (int y = 0; y < SCREEN_H; y += 3) {
            DrawLine(0, y, SCREEN_W, y, (Color){ 0, 0, 0, 25 });
        }
        
        // Vignette shadow
        DrawRectangleGradientV(0, 0, SCREEN_W, 40, (Color){ 0, 0, 0, 160 }, (Color){ 0, 0, 0, 0 });
        DrawRectangleGradientV(0, SCREEN_H - 40, SCREEN_W, 40, (Color){ 0, 0, 0, 0 }, (Color){ 0, 0, 0, 160 });
        
        EndDrawing();
    }

    // Free resources
    UnloadTexture(texAssassin);
    UnloadTexture(texGuard);
    
    // Close Audio device
    UnloadAudioStream(stream);
    CloseAudioDevice();

    CloseWindow();

    return 0;
}
