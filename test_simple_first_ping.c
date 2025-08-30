#define TEST_BUILD
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

// Mock Canvas type
typedef void Canvas;

// Mock furi functions
uint32_t furi_get_tick(void) { return 1000; }
void furi_log_print_format(int level, const char* tag, const char* format, ...) {}

// Mock canvas functions
void canvas_draw_dot(Canvas* canvas, int x, int y) {}
void canvas_draw_circle(Canvas* canvas, int x, int y, int radius) {}
void canvas_draw_line(Canvas* canvas, float x1, float y1, float x2, float y2) {}
void canvas_draw_disc(Canvas* canvas, int x, int y, int radius) {}
void canvas_printf(Canvas* canvas, int x, int y, const char* format, ...) {}

// Include core implementations directly
#include "terrain.c"
#include "raycaster.c"
#include "sonar_chart.c"

// Simplified chunk manager for testing
typedef struct {
    TerrainManager* terrain;
    float player_x, player_y;
    uint32_t dummy; // Keep struct non-empty
} SimpleChunkManager;

SimpleChunkManager* simple_chunk_manager_alloc() {
    SimpleChunkManager* manager = calloc(1, sizeof(SimpleChunkManager));
    if (!manager) return NULL;
    
    // Create terrain at submarine location
    manager->terrain = terrain_manager_alloc(12345, 90);
    manager->player_x = 64.0f;
    manager->player_y = 32.0f;
    
    return manager;
}

void simple_chunk_manager_free(SimpleChunkManager* manager) {
    if (manager) {
        if (manager->terrain) {
            terrain_manager_free(manager->terrain);
        }
        free(manager);
    }
}

bool simple_collision_check(int16_t x, int16_t y, void* context) {
    SimpleChunkManager* manager = (SimpleChunkManager*)context;
    
    // Convert world coordinates to local terrain coordinates
    // Assume terrain is centered around submarine position
    int local_x = x - (int)manager->player_x + TERRAIN_SIZE/2;
    int local_y = y - (int)manager->player_y + TERRAIN_SIZE/2;
    
    // Check bounds
    if (local_x < 0 || local_x >= TERRAIN_SIZE || local_y < 0 || local_y >= TERRAIN_SIZE) {
        return false; // Water outside terrain
    }
    
    return terrain_check_collision(manager->terrain, local_x, local_y);
}

int main(void) {
    printf("=== Simple First Ping Test ===\n\n");
    
    // Initialize components
    SimpleChunkManager* chunk_manager = simple_chunk_manager_alloc();
    if (!chunk_manager) {
        printf("ERROR: Failed to allocate chunk manager\n");
        return 1;
    }
    
    Raycaster* raycaster = raycaster_alloc();
    if (!raycaster) {
        printf("ERROR: Failed to allocate raycaster\n");
        simple_chunk_manager_free(chunk_manager);
        return 1;
    }
    
    SonarChart* sonar_chart = sonar_chart_alloc();
    if (!sonar_chart) {
        printf("ERROR: Failed to allocate sonar chart\n");
        raycaster_free(raycaster);
        simple_chunk_manager_free(chunk_manager);
        return 1;
    }
    
    printf("All components initialized successfully\n");
    
    // Submarine position
    float sub_x = 64.0f;
    float sub_y = 32.0f;
    printf("Submarine at: (%.1f, %.1f)\n\n", sub_x, sub_y);
    
    // Check terrain around submarine
    printf("=== Terrain around submarine ===\n");
    int terrain_count = 0;
    int check_radius = 5;
    
    for (int dy = -check_radius; dy <= check_radius; dy++) {
        printf("Row %2d: ", dy);
        for (int dx = -check_radius; dx <= check_radius; dx++) {
            bool collision = simple_collision_check((int16_t)(sub_x + dx), (int16_t)(sub_y + dy), chunk_manager);
            printf("%c", collision ? '#' : '.');
            if (collision) terrain_count++;
        }
        printf("\n");
    }
    printf("Terrain pixels in %dx%d area: %d\n\n", check_radius*2+1, check_radius*2+1, terrain_count);
    
    // Test raycasting
    printf("=== First Ping Raycasting ===\n");
    
    // Get ray pattern
    RayPattern* pattern = raycaster_get_adaptive_pattern(raycaster, false);
    printf("Ray pattern: %d rays, max radius: %d\n", pattern->direction_count, pattern->max_radius);
    
    // Cast rays
    RayResult results[RAY_CACHE_SIZE];
    memset(results, 0, sizeof(results));
    
    uint16_t hits = raycaster_cast_pattern(
        raycaster,
        pattern,
        (int16_t)sub_x,
        (int16_t)sub_y,
        results,
        simple_collision_check,
        chunk_manager
    );
    
    printf("Raycasting result: %d hits out of %d rays\n", hits, pattern->direction_count);
    printf("Rays cast this frame: %d\n", raycaster->rays_cast_this_frame);
    
    // Analyze results
    uint16_t terrain_hits = 0;
    uint16_t water_hits = 0;
    int min_distance = INT16_MAX;
    int max_distance = 0;
    
    printf("\nRay results:\n");
    for (uint16_t i = 0; i < pattern->direction_count && i < 16; i++) { // Show first 16
        RayResult* result = &results[i];
        
        if (result->ray_complete) {
            if (result->hit_terrain) {
                terrain_hits++;
                printf("  Ray %2d: TERRAIN at (%d,%d) distance=%d\n", 
                       i, result->hit_x, result->hit_y, result->distance);
            } else {
                water_hits++;
            }
            
            if (result->distance < min_distance) min_distance = result->distance;
            if (result->distance > max_distance) max_distance = result->distance;
        } else {
            printf("  Ray %2d: INCOMPLETE\n", i);
        }
    }
    
    printf("\nSummary:\n");
    printf("  Terrain hits: %d\n", terrain_hits);
    printf("  Water hits: %d\n", water_hits);
    printf("  Distance range: %d to %d\n", min_distance == INT16_MAX ? 0 : min_distance, max_distance);
    
    // Simulate progressive ping
    printf("\n=== Progressive Ping Simulation ===\n");
    uint16_t total_points_added = 0;
    
    for (int ping_radius = 0; ping_radius <= 64; ping_radius += 2) {
        uint16_t points_this_radius = 0;
        
        for (uint16_t i = 0; i < pattern->direction_count; i++) {
            RayResult* result = &results[i];
            
            if (result->ray_complete && result->distance <= ping_radius && result->distance > ping_radius - 2) {
                bool added = sonar_chart_add_point(sonar_chart, result->hit_x, result->hit_y, result->hit_terrain);
                if (added && result->hit_terrain) {
                    points_this_radius++;
                    total_points_added++;
                }
            }
        }
        
        if (points_this_radius > 0) {
            printf("  Radius %2d: +%d terrain points (total: %d)\n", 
                   ping_radius, points_this_radius, total_points_added);
        }
        
        // Early radius check (this is where the bug would show)
        if (ping_radius <= 4) {
            printf("    -> At radius %d: %d points discovered so far\n", ping_radius, total_points_added);
            if (total_points_added <= 3) {
                printf("    -> *** BUG DETECTED: Only %d points at early radius! ***\n", total_points_added);
            }
        }
    }
    
    printf("\nFinal result: %d terrain points discovered\n", total_points_added);
    
    if (total_points_added <= 3) {
        printf("\n*** REPRODUCED THE BUG: Only %d terrain points total! ***\n", total_points_added);
    } else {
        printf("\n*** Test passed: %d terrain points found ***\n", total_points_added);
    }
    
    // Cleanup
    sonar_chart_free(sonar_chart);
    raycaster_free(raycaster);
    simple_chunk_manager_free(chunk_manager);
    
    return 0;
}