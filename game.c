#include "game.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static const EntityDescription submarine_desc;
static const EntityDescription torpedo_desc;
static const LevelBehaviour level;

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
    entity_pos_set(self, (Vector){27, 32});
    
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
    
    // Handle movement controls (rotated 90° counter-clockwise)
    // Up button now controls left turn, Down button controls right turn
    // Left button now controls forward thrust, Right button controls reverse
    if(input.held & GameKeyUp) {
        game_context->heading -= game_context->turn_rate;
        if(game_context->heading < 0) game_context->heading += 1.0f;
    }
    if(input.held & GameKeyDown) {
        game_context->heading += game_context->turn_rate;
        if(game_context->heading >= 1.0f) game_context->heading -= 1.0f;
    }
    if(input.held & GameKeyLeft) {
        game_context->velocity += game_context->acceleration;
        if(game_context->velocity > game_context->max_velocity) {
            game_context->velocity = game_context->max_velocity;
        }
    }
    if(input.held & GameKeyRight) {
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
    
    // Update ping with terrain detection
    if(game_context->ping_active) {
        uint32_t current_time = furi_get_tick();
        if(current_time - game_context->ping_timer > 50) { // Update every 50ms
            game_context->ping_radius += 2;
            game_context->ping_timer = current_time;
            
            // Perform raycasting to detect terrain
            if(game_context->terrain && game_context->sonar_chart) {
                for(float angle = 0; angle < 2 * 3.14159f; angle += 0.1f) {
                    int ray_x = (int)(game_context->ping_x + cosf(angle) * game_context->ping_radius);
                    int ray_y = (int)(game_context->ping_y + sinf(angle) * game_context->ping_radius);
                    
                    if(ray_x >= 0 && ray_x < game_context->chart_width && 
                       ray_y >= 0 && ray_y < game_context->chart_height) {
                        
                        // Mark as discovered in sonar chart
                        int chart_idx = ray_y * game_context->chart_width + ray_x;
                        game_context->sonar_chart[chart_idx] = true;
                    }
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

// Rotate coordinates 90° counter-clockwise: (x,y) -> (y, 127-x)
typedef struct {
    int x;
    int y;
} RotatedPoint;

static RotatedPoint rotate_coords(int x, int y) {
    // Rotate 90 degrees counter-clockwise: (x,y) -> (63-y, x)
    return (RotatedPoint){63 - y, x};
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
                
                // Check if this world coordinate is in bounds and discovered
                if(world_x >= 0 && world_x < game_context->chart_width &&
                   world_y >= 0 && world_y < game_context->chart_height) {
                    
                    int chart_idx = world_y * game_context->chart_width + world_x;
                    if(game_context->sonar_chart[chart_idx] && 
                       terrain_check_collision(game_context->terrain, world_x, world_y)) {
                        
                        // Transform world coordinates to screen
                        ScreenPoint screen = world_to_screen(game_context, world_x, world_y);
                        
                        // Only draw if on screen
                        if(screen.screen_x >= 0 && screen.screen_x < 128 &&
                           screen.screen_y >= 0 && screen.screen_y < 64) {
                            RotatedPoint rotated = rotate_coords(screen.screen_x, screen.screen_y);
                            canvas_draw_dot(canvas, rotated.x, rotated.y);
                        }
                    }
                }
            }
        }
    }
    
    // Draw submarine at bottom center (where player starts)
    RotatedPoint sub_pos = rotate_coords(game_context->screen_x, game_context->screen_y);
    canvas_draw_disc(canvas, sub_pos.x, sub_pos.y, 2);
    
    // Draw heading indicator pointing up on screen
    float head_x = game_context->screen_x;
    float head_y = game_context->screen_y + 8; // Point down in original coords (up after rotation)
    RotatedPoint sub_rotated = rotate_coords(game_context->screen_x, game_context->screen_y);
    RotatedPoint head_rotated = rotate_coords(head_x, head_y);
    canvas_draw_line(canvas, sub_rotated.x, sub_rotated.y, head_rotated.x, head_rotated.y);
    
    // Draw velocity vector or torpedo targeting
    if(game_context->mode == GAME_MODE_NAV && game_context->velocity > 0.01f) {
        // Navigation mode: show velocity vector pointing up
        float vel_x = game_context->screen_x;
        float vel_y = game_context->screen_y + 8 + game_context->velocity * 20;
        RotatedPoint vel_rotated = rotate_coords(vel_x, vel_y);
        canvas_draw_line(canvas, head_rotated.x, head_rotated.y, vel_rotated.x, vel_rotated.y);
    } else if(game_context->mode == GAME_MODE_TORPEDO) {
        // Torpedo mode: show targeting cone
        float range = 30.0f;
        float cone_offset = 8.0f;
        
        float target1_x = game_context->screen_x - cone_offset;
        float target1_y = game_context->screen_y + range;
        float target2_x = game_context->screen_x + cone_offset;
        float target2_y = game_context->screen_y + range;
        
        RotatedPoint target1_rotated = rotate_coords(target1_x, target1_y);
        RotatedPoint target2_rotated = rotate_coords(target2_x, target2_y);
        canvas_draw_line(canvas, sub_rotated.x, sub_rotated.y, target1_rotated.x, target1_rotated.y);
        canvas_draw_line(canvas, sub_rotated.x, sub_rotated.y, target2_rotated.x, target2_rotated.y);
    }
    
    // Draw ping
    if(game_context->ping_active) {
        ScreenPoint ping_screen = world_to_screen(game_context, game_context->ping_x, game_context->ping_y);
        RotatedPoint ping_rotated = rotate_coords(ping_screen.screen_x, ping_screen.screen_y);
        canvas_draw_circle(canvas, ping_rotated.x, ping_rotated.y, game_context->ping_radius);
    }
    
    // Draw HUD directly at screen coordinates (no rotation for text)
    // Since the screen is now 64x128, place HUD at actual top
    canvas_printf(canvas, 2, 2, "V:%.2f H:%.2f", (double)game_context->velocity, (double)game_context->heading);
    canvas_printf(canvas, 2, 12, "%s T:%d/%d", 
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
    
    // Initialize sonar chart (same size as screen for now)
    game_context->chart_width = 128;
    game_context->chart_height = 64;
    size_t chart_size = game_context->chart_width * game_context->chart_height;
    game_context->sonar_chart = malloc(chart_size * sizeof(bool));
    if(game_context->sonar_chart) {
        memset(game_context->sonar_chart, 0, chart_size * sizeof(bool));
    }
    
    // Set submarine screen position (will appear at bottom center after rotation)
    // Using rotation (x,y) -> (63-y, x): to get (32, 100) we need (32, 31)
    game_context->screen_x = 32;  // Horizontal center
    game_context->screen_y = -37; // This will rotate to bottom of screen
    
    // Find a safe starting world position in water
    game_context->world_x = 27;
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
                    
                    // Keep within terrain bounds with bigger margin
                    if(test_x >= 15 && test_x < game_context->chart_width - 15 &&
                       test_y >= 15 && test_y < game_context->chart_height - 15) {
                        
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
    
    // Game settings
    game_context->max_velocity = 0.1f;
    game_context->turn_rate = 0.002f;
    game_context->acceleration = 0.002f;
    
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
    if(game_context->sonar_chart) {
        free(game_context->sonar_chart);
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