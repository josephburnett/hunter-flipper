#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static const EntityDescription submarine_desc;
static const EntityDescription torpedo_desc;
static const LevelBehaviour level;

/****** Fading Sonar Chart System - Optimized ******/

// Simple hash function for coordinates
static uint32_t sonar_hash(int x, int y) {
    return ((uint32_t)x * 73 + (uint32_t)y * 151) & (SONAR_HASH_SIZE - 1);
}

SonarChart* sonar_chart_alloc(void) {
    SonarChart* chart = malloc(sizeof(SonarChart));
    if(!chart) return NULL;
    
    // Allocate hash table: 256 buckets * (4 points * 8 bytes + 1 byte count) = ~8KB
    chart->buckets = calloc(SONAR_HASH_SIZE, sizeof(SonarBucket));
    if(!chart->buckets) {
        free(chart);
        return NULL;
    }
    
    chart->last_cleanup_time = 0;
    return chart;
}

void sonar_chart_free(SonarChart* chart) {
    if(!chart) return;
    if(chart->buckets) free(chart->buckets);
    free(chart);
}

void sonar_chart_add_point(SonarChart* chart, int x, int y, uint32_t current_time) {
    if(!chart || !chart->buckets) return;
    
    // Bounds check for 16-bit coordinates
    if(x < -32767 || x > 32767 || y < -32767 || y > 32767) return;
    
    uint32_t hash = sonar_hash(x, y);
    if(hash >= SONAR_HASH_SIZE) return; // Safety check
    
    SonarBucket* bucket = &chart->buckets[hash];
    
    // Check if point already exists in bucket
    for(uint8_t i = 0; i < bucket->count && i < SONAR_MAX_PER_BUCKET; i++) {
        if(bucket->points[i].x == x && bucket->points[i].y == y) {
            bucket->points[i].discovered_time = current_time;
            return;
        }
    }
    
    // Add new point if bucket has space
    if(bucket->count < SONAR_MAX_PER_BUCKET) {
        bucket->points[bucket->count].x = (int16_t)x;
        bucket->points[bucket->count].y = (int16_t)y;
        bucket->points[bucket->count].discovered_time = current_time;
        bucket->count++;
    } else {
        // Replace oldest point in bucket
        uint32_t oldest_time = current_time;
        uint8_t oldest_idx = 0;
        for(uint8_t i = 0; i < SONAR_MAX_PER_BUCKET; i++) {
            if(bucket->points[i].discovered_time < oldest_time) {
                oldest_time = bucket->points[i].discovered_time;
                oldest_idx = i;
            }
        }
        if(oldest_idx < SONAR_MAX_PER_BUCKET) {
            bucket->points[oldest_idx].x = (int16_t)x;
            bucket->points[oldest_idx].y = (int16_t)y;
            bucket->points[oldest_idx].discovered_time = current_time;
        }
    }
}

bool sonar_chart_is_discovered(SonarChart* chart, int x, int y, uint32_t current_time) {
    if(!chart || !chart->buckets) return false;
    
    // Bounds check
    if(x < -32767 || x > 32767 || y < -32767 || y > 32767) return false;
    
    uint32_t hash = sonar_hash(x, y);
    if(hash >= SONAR_HASH_SIZE) return false;
    
    SonarBucket* bucket = &chart->buckets[hash];
    
    for(uint8_t i = 0; i < bucket->count && i < SONAR_MAX_PER_BUCKET; i++) {
        if(bucket->points[i].x == x && bucket->points[i].y == y) {
            // Check if point hasn't faded yet
            if(current_time - bucket->points[i].discovered_time <= SONAR_FADE_DURATION_MS) {
                return true;
            }
        }
    }
    return false;
}

void sonar_chart_cleanup_old_points(SonarChart* chart, uint32_t current_time) {
    if(!chart || !chart->buckets) return;
    
    // Only cleanup every 2 seconds to reduce CPU load
    if(current_time - chart->last_cleanup_time < 2000) return;
    chart->last_cleanup_time = current_time;
    
    // Clean up every 4th bucket each time (spread the work over time)
    static uint32_t cleanup_offset = 0;
    uint32_t buckets_per_cleanup = SONAR_HASH_SIZE / 4;
    if(buckets_per_cleanup == 0) buckets_per_cleanup = 1;
    
    for(uint32_t i = 0; i < buckets_per_cleanup; i++) {
        uint32_t bucket_idx = (cleanup_offset + i) % SONAR_HASH_SIZE;
        if(bucket_idx >= SONAR_HASH_SIZE) continue;
        
        SonarBucket* bucket = &chart->buckets[bucket_idx];
        
        uint8_t write_idx = 0;
        for(uint8_t read_idx = 0; read_idx < bucket->count && read_idx < SONAR_MAX_PER_BUCKET; read_idx++) {
            // Keep points that haven't faded
            if(current_time - bucket->points[read_idx].discovered_time <= SONAR_FADE_DURATION_MS) {
                if(write_idx != read_idx && write_idx < SONAR_MAX_PER_BUCKET) {
                    bucket->points[write_idx] = bucket->points[read_idx];
                }
                if(write_idx < SONAR_MAX_PER_BUCKET) write_idx++;
            }
        }
        bucket->count = write_idx;
    }
    
    cleanup_offset = (cleanup_offset + buckets_per_cleanup) % SONAR_HASH_SIZE;
}

float sonar_chart_get_fade_level(SonarChart* chart, int x, int y, uint32_t current_time) {
    if(!chart || !chart->buckets) return 0.0f;
    
    uint32_t hash = sonar_hash(x, y);
    SonarBucket* bucket = &chart->buckets[hash];
    
    for(uint8_t i = 0; i < bucket->count; i++) {
        if(bucket->points[i].x == x && bucket->points[i].y == y) {
            uint32_t age = current_time - bucket->points[i].discovered_time;
            if(age <= SONAR_FADE_DURATION_MS) {
                // Return fade level: 1.0 = fully visible, 0.0 = fully faded
                return 1.0f - ((float)age / (float)SONAR_FADE_DURATION_MS);
            }
        }
    }
    return 0.0f;
}

/****** Camera/Coordinate System ******/

typedef struct {
    float screen_x;
    float screen_y;
} ScreenPoint;

// Transform world coordinates to screen coordinates relative to submarine
static ScreenPoint world_to_screen(GameContext* ctx, float world_x, float world_y) {
    // Translate relative to submarine position
    float rel_x = world_x - ctx->world_x;
    float rel_y = world_y - ctx->world_y;
    
    // Rotate around submarine (submarine always points "up" on screen)
    float cos_h = cosf(-ctx->heading * 2 * 3.14159f);  // Negative for counter-rotation
    float sin_h = sinf(-ctx->heading * 2 * 3.14159f);
    
    float rot_x = rel_x * cos_h - rel_y * sin_h;
    float rot_y = rel_x * sin_h + rel_y * cos_h;
    
    // Translate to screen coordinates (submarine at center)
    ScreenPoint screen;
    screen.screen_x = ctx->screen_x + rot_x;
    screen.screen_y = ctx->screen_y + rot_y;  // Note: Y points down in screen coords
    
    return screen;
}

// Transform screen coordinates back to world coordinates (for future use)
__attribute__((unused)) static void screen_to_world(GameContext* ctx, float screen_x, float screen_y, float* world_x, float* world_y) {
    // Translate relative to submarine screen position
    float rel_x = screen_x - ctx->screen_x;
    float rel_y = screen_y - ctx->screen_y;
    
    // Rotate to world coordinates
    float cos_h = cosf(ctx->heading * 2 * 3.14159f);
    float sin_h = sinf(ctx->heading * 2 * 3.14159f);
    
    float rot_x = rel_x * cos_h - rel_y * sin_h;
    float rot_y = rel_x * sin_h + rel_y * cos_h;
    
    // Translate to world coordinates
    *world_x = ctx->world_x + rot_x;
    *world_y = ctx->world_y + rot_y;
}

/****** Input Handling ******/

static void handle_input(GameManager* manager, GameContext* game_context) {
    InputState input = game_manager_input_get(manager);
    uint32_t current_time = furi_get_tick();
    
    // Handle back button long/short press
    if(input.pressed & GameKeyBack) {
        game_context->back_press_start = current_time;
        game_context->back_long_press = false;
    }
    
    if(input.held & GameKeyBack) {
        if(current_time - game_context->back_press_start > 1000) { // 1 second for long press
            if(!game_context->back_long_press) {
                // Long press detected - exit game
                game_manager_game_stop(manager);
                return;
            }
        }
    }
    
    if(input.released & GameKeyBack) {
        if(current_time - game_context->back_press_start < 1000) {
            // Short press - toggle mode
            game_context->mode = (game_context->mode == GAME_MODE_NAV) ? 
                                 GAME_MODE_TORPEDO : GAME_MODE_NAV;
        }
    }
}

/****** Entities: Submarine ******/

typedef struct {
    GameContext* game_context;
} SubmarineContext;

static void submarine_start(Entity* self, GameManager* manager, void* context) {
    UNUSED(self);
    UNUSED(manager);
    SubmarineContext* sub_context = context;
    
    // Initialize submarine at center of screen
    entity_pos_set(self, (Vector){64, 32});
    
    // Add collision detection
    entity_collider_add_circle(self, 2);
    
    // Get game context reference
    sub_context->game_context = game_manager_game_context_get(manager);
}

static void submarine_update(Entity* self, GameManager* manager, void* context) {
    SubmarineContext* sub_context = context;
    GameContext* game_context = sub_context->game_context;
    
    // Handle input first
    handle_input(manager, game_context);
    
    // Cleanup old sonar points periodically (every frame is fine, it's fast)
    sonar_chart_cleanup_old_points(game_context->sonar_chart, furi_get_tick());
    
    InputState input = game_manager_input_get(manager);
    
    // Handle movement controls
    if(input.held & GameKeyLeft) {
        game_context->heading -= game_context->turn_rate;
        if(game_context->heading < 0) game_context->heading += 1.0f;
    }
    if(input.held & GameKeyRight) {
        game_context->heading += game_context->turn_rate;
        if(game_context->heading >= 1.0f) game_context->heading -= 1.0f;
    }
    if(input.held & GameKeyUp) {
        game_context->velocity += game_context->acceleration;
        if(game_context->velocity > game_context->max_velocity) {
            game_context->velocity = game_context->max_velocity;
        }
    }
    if(input.held & GameKeyDown) {
        game_context->velocity -= game_context->acceleration;
        if(game_context->velocity < 0) game_context->velocity = 0;
    } else if(!(input.held & GameKeyUp)) {
        // Add slight deceleration when not actively accelerating for realistic momentum
        game_context->velocity -= game_context->acceleration * 0.3f;
        if(game_context->velocity < 0) game_context->velocity = 0;
    }
    
    // Handle OK button (ping or fire)
    if(input.pressed & GameKeyOk) {
        if(game_context->mode == GAME_MODE_NAV) {
            // Start ping
            if(!game_context->ping_active) {
                game_context->ping_active = true;
                game_context->ping_x = game_context->world_x;
                game_context->ping_y = game_context->world_y;
                game_context->ping_radius = 0;
                game_context->ping_timer = furi_get_tick();
            }
        } else {
            // Fire torpedo
            if(game_context->torpedo_count < game_context->max_torpedoes) {
                Level* current_level = game_manager_current_level_get(manager);
                Entity* torpedo = level_add_entity(current_level, &torpedo_desc);
                if(torpedo) {
                    // Set torpedo initial world position and screen position
                    entity_pos_set(torpedo, (Vector){game_context->screen_x, game_context->screen_y});
                    game_context->torpedo_count++;
                }
            }
        }
    }
    
    // Update ping with terrain detection
    if(game_context->ping_active) {
        uint32_t current_time = furi_get_tick();
        if(current_time - game_context->ping_timer > 60) { // Update every 60ms for better performance
            game_context->ping_radius += 2;
            game_context->ping_timer = current_time;
            
            // Perform smart raycasting with adaptive density
            if(game_context->terrain && game_context->sonar_chart) {
                uint32_t current_time = furi_get_tick();
                
                // Adaptive ray density based on ping radius
                float angle_step;
                if(game_context->ping_radius <= 15) {
                    // Close range: dense rays for detail
                    angle_step = 0.1f;
                } else if(game_context->ping_radius <= 30) {
                    // Medium range: balanced density
                    angle_step = 0.15f;
                } else {
                    // Long range: sparse rays for coverage
                    angle_step = 0.2f;
                }
                
                // Cast rays with adaptive density
                for(float angle = 0; angle < 2 * 3.14159f; angle += angle_step) {
                    int ray_x = (int)(game_context->ping_x + cosf(angle) * game_context->ping_radius);
                    int ray_y = (int)(game_context->ping_y + sinf(angle) * game_context->ping_radius);
                    
                    // Add to fading sonar chart
                    sonar_chart_add_point(game_context->sonar_chart, ray_x, ray_y, current_time);
                }
            }
            
            if(game_context->ping_radius > 40) { // Max ping radius (reduced for screen size)
                game_context->ping_active = false;
            }
        }
    }
    
    // Update submarine world position
    // Adjust heading so 0 = forward (negative Y), matching screen orientation
    float movement_heading = (game_context->heading - 0.25f) * 2 * 3.14159f;
    float dx = game_context->velocity * cosf(movement_heading);
    float dy = game_context->velocity * sinf(movement_heading);
    
    float new_world_x = game_context->world_x + dx;
    float new_world_y = game_context->world_y + dy;
    
    // Check terrain collision in world coordinates
    if(game_context->terrain && terrain_check_collision(game_context->terrain, (int)new_world_x, (int)new_world_y)) {
        // Stop submarine if hitting terrain
        game_context->velocity = 0;
    } else {
        game_context->world_x = new_world_x;
        game_context->world_y = new_world_y;
    }
    
    // Submarine screen position is always centered (no boundary checks needed)
    // Update entity position to screen center
    entity_pos_set(self, (Vector){game_context->screen_x, game_context->screen_y});
}

static void submarine_render(Entity* self, GameManager* manager, Canvas* canvas, void* context) {
    UNUSED(self);
    UNUSED(manager);
    SubmarineContext* sub_context = context;
    GameContext* game_context = sub_context->game_context;
    
    // Draw terrain - transform world coordinates to screen
    if(game_context->terrain && game_context->sonar_chart) {
        // Sample terrain around submarine's world position
        int sample_radius = 80; // How far to sample around submarine
        
        for(int world_y = (int)game_context->world_y - sample_radius; 
            world_y <= (int)game_context->world_y + sample_radius; world_y++) {
            for(int world_x = (int)game_context->world_x - sample_radius; 
                world_x <= (int)game_context->world_x + sample_radius; world_x++) {
                
                // Check if this world coordinate is discovered and draw with fading
                uint32_t current_time = furi_get_tick();
                if(sonar_chart_is_discovered(game_context->sonar_chart, world_x, world_y, current_time) && 
                   terrain_check_collision(game_context->terrain, world_x, world_y)) {
                    
                    // Transform world coordinates to screen
                    ScreenPoint screen = world_to_screen(game_context, world_x, world_y);
                    
                    // Only draw if on screen
                    if(screen.screen_x >= 0 && screen.screen_x < 128 &&
                       screen.screen_y >= 0 && screen.screen_y < 64) {
                        
                        // Get fade level for fading effect (optional visual enhancement)
                        float fade_level = sonar_chart_get_fade_level(game_context->sonar_chart, world_x, world_y, current_time);
                        
                        // For now, just draw the dot (later we could implement fading visuals)
                        if(fade_level > 0.3f) { // Only show if not too faded
                            canvas_draw_dot(canvas, screen.screen_x, screen.screen_y);
                        }
                    }
                }
            }
        }
    }
    
    // Draw submarine (always centered and pointing up)
    canvas_draw_disc(canvas, game_context->screen_x, game_context->screen_y, 2);
    
    // Draw heading indicator (always pointing up on screen)
    float head_x = game_context->screen_x;
    float head_y = game_context->screen_y - 8; // Point up
    canvas_draw_line(canvas, game_context->screen_x, game_context->screen_y, head_x, head_y);
    
    // Draw velocity vector or torpedo targeting
    if(game_context->mode == GAME_MODE_NAV && game_context->velocity > 0.01f) {
        // Navigation mode: show velocity vector (always pointing up)
        float vel_x = game_context->screen_x;
        float vel_y = game_context->screen_y - 8 - game_context->velocity * 20;
        canvas_draw_line(canvas, head_x, head_y, vel_x, vel_y);
    } else if(game_context->mode == GAME_MODE_TORPEDO) {
        // Torpedo mode: show targeting cone (symmetric around up direction)
        float range = 30.0f;
        float cone_offset = 8.0f; // pixels offset for cone width
        
        float target1_x = game_context->screen_x - cone_offset;
        float target1_y = game_context->screen_y - range;
        float target2_x = game_context->screen_x + cone_offset;
        float target2_y = game_context->screen_y - range;
        
        canvas_draw_line(canvas, game_context->screen_x, game_context->screen_y, target1_x, target1_y);
        canvas_draw_line(canvas, game_context->screen_x, game_context->screen_y, target2_x, target2_y);
    }
    
    // Draw ping (transform ping center to screen)
    if(game_context->ping_active) {
        ScreenPoint ping_screen = world_to_screen(game_context, game_context->ping_x, game_context->ping_y);
        canvas_draw_circle(canvas, ping_screen.screen_x, ping_screen.screen_y, game_context->ping_radius);
    }
    
    // Draw HUD
    canvas_printf(canvas, 2, 8, "V:%.2f H:%.2f", (double)game_context->velocity, (double)game_context->heading);
    canvas_printf(canvas, 2, 62, "%s T:%d/%d", 
                  game_context->mode == GAME_MODE_NAV ? "NAV" : "TORP",
                  game_context->torpedo_count, game_context->max_torpedoes);
}

static const EntityDescription submarine_desc = {
    .start = submarine_start,
    .stop = NULL,
    .update = submarine_update,
    .render = submarine_render,
    .collision = NULL,
    .event = NULL,
    .context_size = sizeof(SubmarineContext),
};

/****** Entities: Torpedo ******/

typedef struct {
    float world_x;
    float world_y;
    float heading;
    float speed;
    GameContext* game_context;
} TorpedoContext;

static void torpedo_start(Entity* self, GameManager* manager, void* context) {
    UNUSED(self);
    TorpedoContext* torp_context = context;
    GameContext* game_context = game_manager_game_context_get(manager);
    
    // Initialize torpedo with submarine's current world position and heading
    torp_context->world_x = game_context->world_x;
    torp_context->world_y = game_context->world_y;
    torp_context->heading = game_context->heading;
    torp_context->speed = 0.15f; // Faster than submarine max speed
    torp_context->game_context = game_context;
    
    // Add collision detection
    entity_collider_add_circle(self, 1);
}

static void torpedo_update(Entity* self, GameManager* manager, void* context) {
    UNUSED(manager);
    TorpedoContext* torp_context = context;
    
    // Move torpedo in world coordinates
    // Use same heading adjustment as submarine movement
    float movement_heading = (torp_context->heading - 0.25f) * 2 * 3.14159f;
    float dx = torp_context->speed * cosf(movement_heading);
    float dy = torp_context->speed * sinf(movement_heading);
    
    torp_context->world_x += dx;
    torp_context->world_y += dy;
    
    // Check terrain collision in world coordinates
    if(torp_context->game_context->terrain && 
       terrain_check_collision(torp_context->game_context->terrain, (int)torp_context->world_x, (int)torp_context->world_y)) {
        // Torpedo hit terrain - remove it
        Level* current_level = game_manager_current_level_get(manager);
        torp_context->game_context->torpedo_count--;
        level_remove_entity(current_level, self);
        return;
    }
    
    // Check if torpedo is too far from submarine (replace screen boundary check)
    float dist_x = torp_context->world_x - torp_context->game_context->world_x;
    float dist_y = torp_context->world_y - torp_context->game_context->world_y;
    float distance_squared = dist_x * dist_x + dist_y * dist_y;
    
    if(distance_squared > 100 * 100) { // Max range of 100 units
        // Torpedo went too far - remove it
        Level* current_level = game_manager_current_level_get(manager);
        torp_context->game_context->torpedo_count--;
        level_remove_entity(current_level, self);
        return;
    }
    
    // Transform torpedo world position to screen position
    ScreenPoint screen = world_to_screen(torp_context->game_context, torp_context->world_x, torp_context->world_y);
    entity_pos_set(self, (Vector){screen.screen_x, screen.screen_y});
}

static void torpedo_render(Entity* self, GameManager* manager, Canvas* canvas, void* context) {
    UNUSED(manager);
    UNUSED(context);
    Vector pos = entity_pos_get(self);
    
    // Draw torpedo as small filled circle
    canvas_draw_disc(canvas, pos.x, pos.y, 1);
}

static void torpedo_stop(Entity* self, GameManager* manager, void* context) {
    UNUSED(self);
    UNUSED(manager);
    TorpedoContext* torp_context = context;
    
    // Decrement torpedo count when torpedo is destroyed
    if(torp_context->game_context->torpedo_count > 0) {
        torp_context->game_context->torpedo_count--;
    }
}

static const EntityDescription torpedo_desc = {
    .start = torpedo_start,
    .stop = torpedo_stop,
    .update = torpedo_update,
    .render = torpedo_render,
    .collision = NULL,
    .event = NULL,
    .context_size = sizeof(TorpedoContext),
};

/****** Level ******/

static void level_alloc(Level* level, GameManager* manager, void* context) {
    UNUSED(context);
    UNUSED(manager);
    
    // Add submarine entity to the level
    level_add_entity(level, &submarine_desc);
}

static const LevelBehaviour level = {
    .alloc = level_alloc,
    .free = NULL,
    .start = NULL,
    .stop = NULL,
    .context_size = 0,
};

/****** Game ******/

static void game_start(GameManager* game_manager, void* ctx) {
    GameContext* game_context = ctx;
    
    // Initialize terrain system first
    game_context->terrain = terrain_manager_alloc(12345, 0.5f); // seed=12345, elevation=0.5
    
    // Initialize optimized fading sonar chart
    game_context->sonar_chart = sonar_chart_alloc();
    if(!game_context->sonar_chart) {
        // Failed to allocate sonar chart - clean up and exit
        if(game_context->terrain) {
            terrain_manager_free(game_context->terrain);
        }
        return;
    }
    
    // Set submarine screen position (always center)
    game_context->screen_x = 64;  // Center of 128px screen
    game_context->screen_y = 32;  // Center of 64px screen
    
    // Find a safe starting world position in water
    game_context->world_x = 64;
    game_context->world_y = 32;
    
    // Search more thoroughly for water if starting position is in terrain
    if(game_context->terrain) {
        bool found_water = false;
        
        // First check if default position has enough open water around it
        bool default_has_open_water = true;
        for(int dy = -5; dy <= 5 && default_has_open_water; dy++) {
            for(int dx = -5; dx <= 5 && default_has_open_water; dx++) {
                int check_x = (int)game_context->world_x + dx;
                int check_y = (int)game_context->world_y + dy;
                if(terrain_check_collision(game_context->terrain, check_x, check_y)) {
                    default_has_open_water = false;
                }
            }
        }
        
        if(default_has_open_water) {
            found_water = true;
        }
        
        // If not, search in expanding circles for a good water area
        if(!found_water) {
            for(int radius = 10; radius <= 50 && !found_water; radius += 5) {
                for(int angle = 0; angle < 36 && !found_water; angle++) {
                    float test_angle = angle * (2.0f * 3.14159f / 36.0f);
                    int test_x = (int)(game_context->world_x + cosf(test_angle) * radius);
                    int test_y = (int)(game_context->world_y + sinf(test_angle) * radius);
                    
                    // Keep reasonable bounds for spawn area (no longer tied to chart bounds)
                    if(test_x >= 15 && test_x < 1000 && test_y >= 15 && test_y < 1000) {
                        
                        // Check for open water in a 10x10 area around this position
                        bool has_open_water = true;
                        for(int dy = -5; dy <= 5 && has_open_water; dy++) {
                            for(int dx = -5; dx <= 5 && has_open_water; dx++) {
                                int check_x = test_x + dx;
                                int check_y = test_y + dy;
                                if(terrain_check_collision(game_context->terrain, check_x, check_y)) {
                                    has_open_water = false;
                                }
                            }
                        }
                        
                        if(has_open_water) {
                            game_context->world_x = test_x;
                            game_context->world_y = test_y;
                            found_water = true;
                        }
                    }
                }
            }
        }
        
        // No initial sonar coverage - start with blank map
        // Player must use sonar to discover terrain
    }
    game_context->velocity = 0;
    game_context->heading = 0;
    game_context->mode = GAME_MODE_NAV;
    
    game_context->torpedo_count = 0;
    game_context->max_torpedoes = 6;
    
    game_context->ping_active = false;
    game_context->ping_radius = 0;
    
    game_context->back_press_start = 0;
    game_context->back_long_press = false;
    
    // Game settings - improved for better responsiveness
    game_context->max_velocity = 0.25f;   // 2.5x faster top speed
    game_context->turn_rate = 0.006f;     // 3x faster turning  
    game_context->acceleration = 0.008f;  // 4x faster acceleration
    
    // Add level to the game
    game_manager_add_level(game_manager, &level);
}

static void game_stop(void* ctx) {
    GameContext* game_context = ctx;
    
    // Clean up terrain system
    if(game_context->terrain) {
        terrain_manager_free(game_context->terrain);
    }
    
    // Clean up sonar chart
    sonar_chart_free(game_context->sonar_chart);
}

const Game game = {
    .target_fps = 30,
    .show_fps = false,
    .always_backlight = true,
    .start = game_start,
    .stop = game_stop,
    .context_size = sizeof(GameContext),
};