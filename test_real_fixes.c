#define TEST_BUILD
#include "mock_furi.h"
#include "chunk_manager.h"
#include "raycaster.h"
#include "sonar_chart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Mock implementations
uint32_t furi_get_tick(void) {
    return 1000;
}

void furi_log_print_format(int level, const char* tag, const char* format, ...) {
    // Silent for test
    (void)level; (void)tag; (void)format;
}

void canvas_draw_dot(Canvas* canvas, int x, int y) { (void)canvas; (void)x; (void)y; }
void canvas_draw_circle(Canvas* canvas, int x, int y, int radius) { (void)canvas; (void)x; (void)y; (void)radius; }
void canvas_draw_line(Canvas* canvas, float x1, float y1, float x2, float y2) { (void)canvas; (void)x1; (void)y1; (void)x2; (void)y2; }
void canvas_draw_disc(Canvas* canvas, int x, int y, int radius) { (void)canvas; (void)x; (void)y; (void)radius; }
void canvas_printf(Canvas* canvas, int x, int y, const char* format, ...) { (void)canvas; (void)x; (void)y; (void)format; }

bool test_collision_callback(int16_t x, int16_t y, void* context) {
    ChunkManager* chunk_manager = (ChunkManager*)context;
    return chunk_manager_check_collision(chunk_manager, x, y);
}

int main(void) {
    printf("=== Testing Real Source Files After Fixes ===\n\n");
    
    // Test fixed chunk manager
    printf("1. Testing Fixed Chunk Manager\n");
    ChunkManager* chunk_manager = chunk_manager_alloc();
    if (!chunk_manager) {
        printf("ERROR: Failed to allocate chunk manager\n");
        return 1;
    }
    
    // Submarine position (same as test plan)
    float sub_x = 64.0f;
    float sub_y = 32.0f;
    printf("Submarine at: (%.1f, %.1f)\n", sub_x, sub_y);
    
    // Update chunk manager (should load 2x2 grid)
    chunk_manager_update(chunk_manager, sub_x, sub_y);
    
    // Count active chunks
    int loaded_chunks = 0;
    for(int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if(chunk_manager->active_chunks[i] && chunk_manager->active_chunks[i]->is_loaded) {
            loaded_chunks++;
        }
    }
    printf("Loaded chunks: %d (should be 4 for 2x2 grid)\n", loaded_chunks);
    
    // Test coordinate conversion
    printf("\n2. Testing Fixed Coordinate Conversion\n");
    ChunkCoord coord = world_to_chunk_coord(sub_x, sub_y);
    printf("World (%.1f, %.1f) -> Chunk (%d, %d)\n", sub_x, sub_y, coord.chunk_x, coord.chunk_y);
    
    // Check terrain around submarine
    int terrain_count = 0;
    printf("Terrain around submarine (5x5):\n");
    for (int dy = -2; dy <= 2; dy++) {
        printf("Row %2d: ", dy);
        for (int dx = -2; dx <= 2; dx++) {
            bool collision = chunk_manager_check_collision(chunk_manager, (int)sub_x + dx, (int)sub_y + dy);
            printf("%c", collision ? '#' : '.');
            if (collision) terrain_count++;
        }
        printf("\n");
    }
    printf("Terrain pixels: %d\n", terrain_count);
    
    // Test fixed raycaster
    printf("\n3. Testing Fixed Raycaster\n");
    Raycaster* raycaster = raycaster_alloc();
    if (!raycaster) {
        printf("ERROR: Failed to allocate raycaster\n");
        chunk_manager_free(chunk_manager);
        return 1;
    }
    
    printf("Initial quality level: %d (should be 0 for full quality)\n", raycaster->current_quality_level);
    
    // Get ray pattern
    RayPattern* pattern = raycaster_get_adaptive_pattern(raycaster, false);
    printf("Ray pattern: %d rays\n", pattern->direction_count);
    
    // Cast rays
    RayResult results[RAY_CACHE_SIZE];
    memset(results, 0, sizeof(results));
    
    uint16_t hits = raycaster_cast_pattern(
        raycaster, pattern,
        (int16_t)sub_x, (int16_t)sub_y,
        results, test_collision_callback, chunk_manager
    );
    
    printf("Raycasting result: %d terrain hits out of %d rays\n", hits, pattern->direction_count);
    
    // Show first few results
    printf("First 8 ray results:\n");
    for (int i = 0; i < 8 && i < pattern->direction_count; i++) {
        RayResult* result = &results[i];
        if (result->ray_complete && result->hit_terrain) {
            printf("  Ray %d: HIT at (%d,%d) distance=%d\n", 
                   i, result->hit_x, result->hit_y, result->distance);
        } else {
            printf("  Ray %d: miss\n", i);
        }
    }
    
    printf("\n4. Progressive Ping Simulation\n");
    SonarChart* sonar_chart = sonar_chart_alloc();
    if (!sonar_chart) {
        printf("ERROR: Failed to allocate sonar chart\n");
        raycaster_free(raycaster);
        chunk_manager_free(chunk_manager);
        return 1;
    }
    
    // Test with radius 2, 4, 6 (first few frames)
    for (int ping_radius = 2; ping_radius <= 6; ping_radius += 2) {
        int points_added = 0;
        
        for (uint16_t i = 0; i < pattern->direction_count; i++) {
            RayResult* result = &results[i];
            if (result->ray_complete && result->hit_terrain && result->distance <= ping_radius) {
                sonar_chart_add_point(sonar_chart, result->hit_x, result->hit_y, true);
                points_added++;
            }
        }
        
        printf("  Radius %d: %d terrain points added\n", ping_radius, points_added);
    }
    
    printf("\n=== FINAL RESULT ===\n");
    if (hits == 0) {
        printf("❌ FAILED: Still 0 hits - coordinate or raycaster bug remains\n");
        return 1;
    } else if (hits <= 3) {
        printf("⚠️  PARTIAL: Only %d hits - some bugs fixed but others remain\n", hits);
        return 1;
    } else {
        printf("✅ SUCCESS: %d terrain hits found!\n", hits);
        printf("The coordinate and raycaster fixes are working.\n");
        printf("You should now see terrain when pinging in the game!\n");
    }
    
    // Cleanup
    sonar_chart_free(sonar_chart);
    raycaster_free(raycaster);
    chunk_manager_free(chunk_manager);
    
    return 0;
}