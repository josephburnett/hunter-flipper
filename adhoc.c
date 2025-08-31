#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine/engine.h"
#include "game.h"
#include "terrain.h"
#include "raycaster.h"
#include "sonar_chart.h"

// Mock canvas for testing
static int dots_drawn = 0;
static int last_dot_x = -1, last_dot_y = -1;

void canvas_draw_dot(Canvas* canvas, uint8_t x, uint8_t y) {
    (void)canvas; // unused
    printf("[CANVAS] Drawing dot at screen (%d, %d)\n", x, y);
    last_dot_x = x;
    last_dot_y = y;
    dots_drawn++;
}

void canvas_draw_disc(Canvas* canvas, uint8_t x, uint8_t y, uint8_t r) {
    (void)canvas; // unused
    printf("[CANVAS] Drawing disc at screen (%d, %d) radius %d\n", x, y, r);
}

int main() {
    printf("=== RENDER DEBUG TEST ===\n");
    
    // Create minimal game context for testing render
    GameContext* ctx = calloc(1, sizeof(GameContext));
    if(!ctx) return 1;
    
    // Initialize basic position (submarine at 64,64 world coords)
    ctx->world_x = 64;
    ctx->world_y = 64;
    ctx->screen_x = 64;  // center of screen
    ctx->screen_y = 32;  // center of screen
    ctx->heading = 0.0f; // pointing up
    
    // Create terrain and sonar chart
    ctx->chunk_manager = chunk_manager_create();
    ctx->sonar_chart = sonar_chart_create();
    
    // Create a pattern for raycasting
    RayPattern* pattern = ray_pattern_create_circle(32, 16);
    
    // Add some test terrain points manually to sonar chart
    printf("Adding test terrain points...\n");
    sonar_chart_add_point(ctx->sonar_chart, 66, 64, true);   // Right of sub
    sonar_chart_add_point(ctx->sonar_chart, 67, 64, true);   // Further right
    sonar_chart_add_point(ctx->sonar_chart, 68, 64, true);   // Even further right
    sonar_chart_add_point(ctx->sonar_chart, 64, 66, true);   // Below sub
    sonar_chart_add_point(ctx->sonar_chart, 64, 67, true);   // Further below
    
    // Create submarine context
    SubmarineContext sub_ctx = {0};
    sub_ctx.game_context = ctx;
    
    // Create mock entity and call render
    Entity submarine = {0};
    GameManager manager = {0};
    Canvas canvas = {0}; // Mock canvas
    
    printf("\nCalling submarine_render...\n");
    submarine_render(&submarine, &manager, &canvas, &sub_ctx);
    
    printf("\n=== RENDER RESULTS ===\n");
    printf("Total dots drawn: %d\n", dots_drawn);
    printf("Last dot at: (%d, %d)\n", last_dot_x, last_dot_y);
    
    // Cleanup
    sonar_chart_destroy(ctx->sonar_chart);
    chunk_manager_destroy(ctx->chunk_manager);
    ray_pattern_destroy(pattern);
    free(ctx);
    
    return 0;
}