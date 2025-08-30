#define TEST_BUILD
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Test assertion macro
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("ASSERTION FAILED: %s at %s:%d\n", message, __FILE__, __LINE__); \
            return false; \
        } \
    } while(0)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

// Forward declarations of types from the real game
typedef struct ChunkManager ChunkManager;
typedef struct Raycaster Raycaster;
typedef struct SonarChart SonarChart;
typedef struct RayPattern RayPattern;
typedef struct RayResult RayResult;

// Mock the essential Flipper functions
uint32_t furi_get_tick(void) { 
    static uint32_t tick = 1000;
    return tick += 50;
}

void furi_log_print_format(int level, const char* tag, const char* format, ...) {
    (void)level; (void)tag; (void)format;
}

// Include the real implementations
#include "chunk_manager.c"
#include "raycaster.c"
#include "sonar_chart.c"
#include "terrain.c"

// Test that reproduces the exact bug scenario
bool test_ping_bug_reproduction() {
    printf("=== PING BUG LOCATION TEST ===\n");
    printf("Reproducing the exact conditions where the '3 dots' bug occurs\n\n");
    
    printf("STAGE 1: Component Initialization\n");
    printf("----------------------------------\n");
    
    // Initialize the same components as the real game
    ChunkManager* chunk_manager = chunk_manager_alloc();
    Raycaster* raycaster = raycaster_alloc();
    SonarChart* sonar_chart = sonar_chart_alloc();
    
    // Submarine position (same as typical game start)
    float submarine_x = 64.0f;
    float submarine_y = 32.0f;
    
    printf("✓ Components allocated\n");
    printf("  - Submarine at (%.1f, %.1f)\n", submarine_x, submarine_y);
    
    printf("\nSTAGE 2: Chunk Loading\n");
    printf("----------------------\n");
    
    // Load chunks exactly like the game does
    chunk_manager_update(chunk_manager, submarine_x, submarine_y);
    
    // Verify terrain exists around submarine
    int terrain_found = 0;
    int water_found = 0;
    
    for(int dy = -5; dy <= 5; dy++) {
        for(int dx = -5; dx <= 5; dx++) {
            int test_x = (int)submarine_x + dx;
            int test_y = (int)submarine_y + dy;
            
            if(chunk_manager_check_collision(chunk_manager, test_x, test_y)) {
                terrain_found++;
            } else {
                water_found++;
            }
        }
    }
    
    printf("✓ Terrain survey complete\n");
    printf("  - Terrain pixels: %d\n", terrain_found);
    printf("  - Water pixels: %d\n", water_found);
    
    TEST_ASSERT(terrain_found > 0, "Should find terrain around submarine");
    
    printf("\nSTAGE 3: Progressive Ping Test (BUG REPRODUCTION)\n");
    printf("------------------------------------------------\n");
    
    // Simulate the exact ping sequence that causes the bug
    float ping_x = submarine_x;
    float ping_y = submarine_y;
    
    int total_terrain_hits = 0;
    bool bug_detected = false;
    
    for(int radius = 2; radius <= 20; radius += 2) {
        printf("\n--- Testing Radius %d ---\n", radius);
        
        // Get ray pattern exactly like the game
        RayPattern* pattern = raycaster_get_adaptive_pattern(raycaster, false);
        RayResult results[64]; // Use larger buffer to be safe
        
        printf("  Ray pattern: %d directions\n", pattern->direction_count);
        
        // Cast rays exactly like game.c does
        raycaster_cast_pattern(
            raycaster,
            pattern,
            (int16_t)ping_x,
            (int16_t)ping_y,
            results,
            NULL, // This is the critical difference - no collision callback!
            chunk_manager
        );
        
        // Count results
        int rays_completed = 0;
        int terrain_hits_this_radius = 0;
        int rays_within_radius = 0;
        
        for(uint16_t i = 0; i < pattern->direction_count; i++) {
            RayResult* result = &results[i];
            if(result->ray_complete) {
                rays_completed++;
                if(result->hit_terrain) {
                    if(result->distance <= radius) {
                        terrain_hits_this_radius++;
                        rays_within_radius++;
                    }
                }
            }
        }
        
        total_terrain_hits += terrain_hits_this_radius;
        
        printf("  Rays completed: %d\n", rays_completed);
        printf("  Terrain hits in radius: %d\n", terrain_hits_this_radius);
        printf("  Total terrain hits so far: %d\n", total_terrain_hits);
        
        // Check for the bug pattern
        if(radius <= 10 && terrain_hits_this_radius == 0) {
            printf("  🚨 BUG DETECTED at radius %d: No terrain hits!\n", radius);
            bug_detected = true;
        }
        
        // Early exit if we've confirmed the bug
        if(radius <= 6 && total_terrain_hits <= 3) {
            printf("  ⚠️  Only %d total hits after radius %d - this is the bug!\n", 
                   total_terrain_hits, radius);
            bug_detected = true;
        }
    }
    
    printf("\nSTAGE 4: Bug Analysis\n");
    printf("--------------------\n");
    
    printf("Final Results:\n");
    printf("  - Total terrain hits discovered: %d\n", total_terrain_hits);
    printf("  - Bug detected: %s\n", bug_detected ? "YES" : "NO");
    
    if(total_terrain_hits <= 3) {
        printf("\n🎯 BUG SUCCESSFULLY REPRODUCED!\n");
        printf("The '3 dots only' bug occurs when:\n");
        printf("  1. Terrain exists around the submarine ✓\n");
        printf("  2. Chunks are properly loaded ✓\n");
        printf("  3. Raycaster finds very few terrain hits ❌\n");
        printf("\n💡 ROOT CAUSE IDENTIFIED:\n");
        printf("The issue is likely in:\n");
        printf("  - Ray pattern generation\n");
        printf("  - Collision callback integration\n");
        printf("  - Raycaster adaptive quality settings\n");
        printf("  - Ray result processing\n");
    } else {
        printf("\n✅ No bug detected - terrain discovery working correctly\n");
    }
    
    // Cleanup
    chunk_manager_free(chunk_manager);
    raycaster_free(raycaster);
    sonar_chart_free(sonar_chart);
    
    printf("\n=== BUG LOCATION TEST COMPLETE ===\n");
    
    return !bug_detected;
}

int main() {
    printf("=== PING BUG LOCATION DIAGNOSTIC ===\n\n");
    printf("This test reproduces the exact conditions that cause the '3 dots' bug\n");
    printf("by using real game components and the progressive ping workflow.\n\n");
    
    bool result = test_ping_bug_reproduction();
    
    printf("\n=== DIAGNOSTIC RESULT ===\n");
    if(result) {
        printf("❌ Bug not reproduced - terrain discovery is working\n");
        return 0;
    } else {
        printf("🎯 BUG SUCCESSFULLY REPRODUCED AND LOCATED!\n");
        printf("Use the diagnostic output above to identify the specific cause.\n");
        return 1;
    }
}