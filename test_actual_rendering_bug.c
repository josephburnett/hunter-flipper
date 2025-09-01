// Test to reproduce the actual "single pixel land" bug
// This test simulates the exact scenario from the game logs

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "sonar_chart.h"
#include "mock_furi.c"

// Mock game context matching the actual game
typedef struct {
    float world_x, world_y;
    float screen_x, screen_y;
    float heading;
    SonarChart* sonar_chart;
} GameContext;

// Exact world_to_screen function from game.c
typedef struct {
    int screen_x;
    int screen_y;
} ScreenPoint;

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
    screen.screen_y = ctx->screen_y + rot_y;
    
    return screen;
}

int main() {
    printf("=== Actual Rendering Bug Reproduction Test ===\n");
    printf("Simulating exact game scenario from logs\n\n");
    
    // Create chart exactly like in game
    SonarChart* chart = sonar_chart_alloc();
    if(!chart) {
        printf("FAIL: Could not create sonar chart\n");
        return 1;
    }
    
    // Set up game context matching the log
    GameContext ctx = {
        .world_x = 60.0f,
        .world_y = 51.0f, 
        .screen_x = 64.0f,  // Screen center
        .screen_y = 32.0f,
        .heading = 0.0f,
        .sonar_chart = chart
    };
    
    printf("Game context: Sub at (%.1f, %.1f), screen center (%.1f, %.1f)\n", 
           ctx.world_x, ctx.world_y, ctx.screen_x, ctx.screen_y);
    
    // Add terrain points exactly as shown in the log
    printf("\nStep 1: Adding terrain points from log...\n");
    
    struct { int x, y; } terrain_points[] = {
        {66, 51}, {66, 52}, {66, 53}, {66, 48}, {66, 50},
        {70, 57}, {63, 61}, {62, 62}, {60, 63}, {57, 63},
        {48, 55}, {66, 42}, {66, 44}, {66, 47}, {66, 49},
        {69, 55}, {70, 58}, {64, 62}, {61, 61}, {48, 57}
    };
    
    int num_terrain = sizeof(terrain_points) / sizeof(terrain_points[0]);
    int added_count = 0;
    
    for(int i = 0; i < num_terrain; i++) {
        bool added = sonar_chart_add_point(chart, terrain_points[i].x, terrain_points[i].y, true);
        if(added) {
            added_count++;
            printf("  ✓ Added terrain at (%d, %d)\n", terrain_points[i].x, terrain_points[i].y);
        } else {
            printf("  ❌ Failed to add terrain at (%d, %d)\n", terrain_points[i].x, terrain_points[i].y);
        }
    }
    
    printf("Successfully added %d out of %d terrain points\n", added_count, num_terrain);
    
    // Step 2: Query exactly like the game does
    printf("\nStep 2: Performing render query like game...\n");
    
    int sample_radius = 80; // Exactly from game.c
    SonarBounds query_bounds = sonar_bounds_create(
        (int16_t)ctx.world_x - sample_radius,
        (int16_t)ctx.world_y - sample_radius,
        (int16_t)ctx.world_x + sample_radius,
        (int16_t)ctx.world_y + sample_radius
    );
    
    printf("Query bounds: (%d,%d) to (%d,%d)\n", 
           query_bounds.min_x, query_bounds.min_y, 
           query_bounds.max_x, query_bounds.max_y);
    
    SonarPoint* visible_points[512];
    uint16_t point_count = sonar_chart_query_area(chart, query_bounds, visible_points, 512);
    
    int terrain_count = 0;
    for(uint16_t i = 0; i < point_count; i++) {
        if(visible_points[i]->is_terrain) terrain_count++;
    }
    
    printf("Query returned: %d total (%d terrain)\n", point_count, terrain_count);
    
    // Step 3: Check rendering transformation
    printf("\nStep 3: Testing rendering transformation...\n");
    
    int rendered_count = 0;
    for(uint16_t i = 0; i < point_count; i++) {
        SonarPoint* point = visible_points[i];
        
        if(point->is_terrain) {
            // Transform world coordinates to screen exactly like game
            ScreenPoint screen = world_to_screen(&ctx, point->world_x, point->world_y);
            
            // Check if on screen (exactly like game)
            bool on_screen = (screen.screen_x >= 0 && screen.screen_x < 128 &&
                             screen.screen_y >= 0 && screen.screen_y < 64);
            
            printf("  Terrain at world (%d,%d) -> screen (%d,%d) %s\n",
                   point->world_x, point->world_y, 
                   screen.screen_x, screen.screen_y,
                   on_screen ? "ON SCREEN" : "OFF SCREEN");
            
            if(on_screen) rendered_count++;
        }
    }
    
    printf("\nFinal results:\n");
    printf("- Terrain points added: %d\n", added_count);  
    printf("- Terrain points queried: %d\n", terrain_count);
    printf("- Terrain points on screen: %d\n", rendered_count);
    
    if(rendered_count <= 1) {
        printf("\n❌ BUG REPRODUCED: Only %d terrain point(s) would render!\n", rendered_count);
        printf("This matches the 'single pixel land' bug you're experiencing.\n");
        
        // Additional debugging
        printf("\nDEBUG: Checking why terrain points are not being queried...\n");
        
        // Check each terrain point individually
        for(int i = 0; i < num_terrain; i++) {
            SonarBounds exact = sonar_bounds_create(
                terrain_points[i].x, terrain_points[i].y,
                terrain_points[i].x, terrain_points[i].y
            );
            
            SonarPoint* exact_points[5];
            uint16_t exact_count = sonar_chart_query_area(chart, exact, exact_points, 5);
            
            bool in_query_bounds = sonar_bounds_contains_point(query_bounds, 
                                                              terrain_points[i].x, 
                                                              terrain_points[i].y);
            
            printf("  Point (%d,%d): stored=%s, in_query_bounds=%s\n",
                   terrain_points[i].x, terrain_points[i].y,
                   exact_count > 0 ? "YES" : "NO",
                   in_query_bounds ? "YES" : "NO");
        }
        
        return 1;
    } else {
        printf("\n✓ Multiple terrain points render correctly\n");
        return 0;
    }
}