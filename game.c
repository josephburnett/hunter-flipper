#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static const EntityDescription submarine_desc;
static const EntityDescription torpedo_desc;
static const LevelBehaviour level;

// Collision callback for raycasting
static bool collision_check_callback(int16_t x, int16_t y, void* context) {
    ChunkManager* chunk_manager = (ChunkManager*)context;
    return chunk_manager_check_collision(chunk_manager, x, y);
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
    
    // Update ping with advanced terrain detection
    if(game_context->ping_active && game_context->raycaster && game_context->chunk_manager && game_context->sonar_chart) {
        uint32_t current_time = furi_get_tick();
        if(current_time - game_context->ping_timer > 50) { // Update every 50ms
            game_context->ping_radius += 2;
            game_context->ping_timer = current_time;
            
            // Use optimized raycasting with adaptive quality
            RayPattern* pattern = raycaster_get_adaptive_pattern(game_context->raycaster, false);
            RayResult results[RAY_CACHE_SIZE];
            
            // Cast rays from ping center up to current radius
            raycaster_cast_pattern(
                game_context->raycaster, 
                pattern,
                (int16_t)game_context->ping_x, 
                (int16_t)game_context->ping_y,
                results,
                collision_check_callback,
                game_context->chunk_manager
            );
            
            // Add discoveries to sonar chart
            for(uint16_t i = 0; i < pattern->direction_count; i++) {
                RayResult* result = &results[i];
                if(result->ray_complete && result->distance <= game_context->ping_radius) {
                    // Add terrain or water point to sonar chart
                    sonar_chart_add_point(game_context->sonar_chart, 
                                         result->hit_x, result->hit_y, result->hit_terrain);
                    
                    // If ray hit terrain before max radius, also add intermediate water points
                    if(result->hit_terrain && result->distance > 1) {
                        int16_t start_x = (int16_t)game_context->ping_x;
                        int16_t start_y = (int16_t)game_context->ping_y;
                        int16_t dx = result->hit_x - start_x;
                        int16_t dy = result->hit_y - start_y;
                        
                        // Add water points along the ray path (every few steps)
                        for(uint16_t step = 0; step < result->distance; step += 3) {
                            int16_t water_x = start_x + (dx * step) / result->distance;
                            int16_t water_y = start_y + (dy * step) / result->distance;
                            sonar_chart_add_point(game_context->sonar_chart, water_x, water_y, false);
                        }
                    }
                }
            }
            
            if(game_context->ping_radius > 64) { // Increased max radius for infinite terrain
                game_context->ping_active = false;
            }
        }
    }
    
    // Update chunk manager for infinite terrain loading
    if(game_context->chunk_manager) {
        chunk_manager_update(game_context->chunk_manager, game_context->world_x, game_context->world_y);
    }
    
    // Update sonar chart fading
    if(game_context->sonar_chart) {
        sonar_chart_update_fade(game_context->sonar_chart, furi_get_tick());
    }
    
    // Update submarine world position
    // Adjust heading so 0 = forward (negative Y), matching screen orientation
    float movement_heading = (game_context->heading - 0.25f) * 2 * 3.14159f;
    float dx = game_context->velocity * cosf(movement_heading);
    float dy = game_context->velocity * sinf(movement_heading);
    
    float new_world_x = game_context->world_x + dx;
    float new_world_y = game_context->world_y + dy;
    
    // Check terrain collision using chunk manager
    if(game_context->chunk_manager && chunk_manager_check_collision(game_context->chunk_manager, (int)new_world_x, (int)new_world_y)) {
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
    
    // Draw discovered terrain using advanced sonar chart
    if(game_context->sonar_chart) {
        int sample_radius = 80; // Screen area to query
        
        // Query sonar chart for visible area
        SonarBounds query_bounds = sonar_bounds_create(
            (int16_t)game_context->world_x - sample_radius,
            (int16_t)game_context->world_y - sample_radius,
            (int16_t)game_context->world_x + sample_radius,
            (int16_t)game_context->world_y + sample_radius
        );
        
        SonarPoint* visible_points[512]; // Buffer for visible points
        uint16_t point_count = sonar_chart_query_area(game_context->sonar_chart, 
                                                     query_bounds, visible_points, 512);
        
        // Draw discovered points with fading
        for(uint16_t i = 0; i < point_count; i++) {
            SonarPoint* point = visible_points[i];
            
            // Transform world coordinates to screen
            ScreenPoint screen = world_to_screen(game_context, point->world_x, point->world_y);
            
            // Only draw if on screen
            if(screen.screen_x >= 0 && screen.screen_x < 128 &&
               screen.screen_y >= 0 && screen.screen_y < 64) {
                
                if(point->is_terrain) {
                    // Draw terrain with fade-based intensity
                    uint8_t opacity = sonar_fade_state_opacity(point->fade_state);
                    if(opacity > 128) {
                        canvas_draw_dot(canvas, screen.screen_x, screen.screen_y);
                    } else if(opacity > 64) {
                        // Draw dimmer terrain (every other pixel)
                        if(((int)screen.screen_x + (int)screen.screen_y) % 2 == 0) {
                            canvas_draw_dot(canvas, screen.screen_x, screen.screen_y);
                        }
                    } else if(opacity > 32) {
                        // Draw very dim terrain (every fourth pixel)
                        if(((int)screen.screen_x + (int)screen.screen_y) % 4 == 0) {
                            canvas_draw_dot(canvas, screen.screen_x, screen.screen_y);
                        }
                    }
                }
                // Water points are not drawn (negative space)
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
    
    // Check terrain collision using chunk manager
    if(torp_context->game_context->chunk_manager && 
       chunk_manager_check_collision(torp_context->game_context->chunk_manager, (int)torp_context->world_x, (int)torp_context->world_y)) {
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
    
    // Initialize infinite terrain chunk manager
    game_context->chunk_manager = chunk_manager_alloc();
    
    // Initialize advanced sonar chart with fading
    game_context->sonar_chart = sonar_chart_alloc();
    
    // Initialize optimized raycaster
    game_context->raycaster = raycaster_alloc();
    
    // Set submarine screen position (always center)
    game_context->screen_x = 64;  // Center of 128px screen
    game_context->screen_y = 32;  // Center of 64px screen
    
    // Find a safe starting world position in water
    game_context->world_x = 64;
    game_context->world_y = 32;
    
    // Search for safe starting position in water using chunk manager
    if(game_context->chunk_manager) {
        // Update chunk manager with initial position to load terrain
        chunk_manager_update(game_context->chunk_manager, game_context->world_x, game_context->world_y);
        
        bool found_water = false;
        
        // First check if default position has enough open water around it
        bool default_has_open_water = true;
        for(int dy = -5; dy <= 5 && default_has_open_water; dy++) {
            for(int dx = -5; dx <= 5 && default_has_open_water; dx++) {
                int check_x = (int)game_context->world_x + dx;
                int check_y = (int)game_context->world_y + dy;
                if(chunk_manager_check_collision(game_context->chunk_manager, check_x, check_y)) {
                    default_has_open_water = false;
                }
            }
        }
        
        if(default_has_open_water) {
            found_water = true;
        }
        
        // If not, search in expanding circles for a good water area
        if(!found_water) {
            for(int radius = 10; radius <= 200 && !found_water; radius += 10) {
                for(int angle = 0; angle < 36 && !found_water; angle++) {
                    float test_angle = angle * (2.0f * 3.14159f / 36.0f);
                    int test_x = (int)(game_context->world_x + cosf(test_angle) * radius);
                    int test_y = (int)(game_context->world_y + sinf(test_angle) * radius);
                    
                    // Update chunk manager for this test position
                    chunk_manager_update(game_context->chunk_manager, (float)test_x, (float)test_y);
                    
                    // Check for open water in a 10x10 area around this position
                    bool has_open_water = true;
                    for(int dy = -5; dy <= 5 && has_open_water; dy++) {
                        for(int dx = -5; dx <= 5 && has_open_water; dx++) {
                            int check_x = test_x + dx;
                            int check_y = test_y + dy;
                            if(chunk_manager_check_collision(game_context->chunk_manager, check_x, check_y)) {
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
    
    // Game settings
    game_context->max_velocity = 0.1f;
    game_context->turn_rate = 0.002f;
    game_context->acceleration = 0.002f;
    
    // Add level to the game
    game_manager_add_level(game_manager, &level);
}

static void game_stop(void* ctx) {
    GameContext* game_context = ctx;
    
    // Clean up chunk manager (includes terrain cleanup)
    if(game_context->chunk_manager) {
        chunk_manager_free(game_context->chunk_manager);
    }
    
    // Clean up advanced sonar chart
    if(game_context->sonar_chart) {
        sonar_chart_free(game_context->sonar_chart);
    }
    
    // Clean up raycaster
    if(game_context->raycaster) {
        raycaster_free(game_context->raycaster);
    }
}

const Game game = {
    .target_fps = 30,
    .show_fps = false,
    .always_backlight = true,
    .start = game_start,
    .stop = game_stop,
    .context_size = sizeof(GameContext),
};