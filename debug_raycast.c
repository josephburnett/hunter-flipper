#define TEST_BUILD
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

// Mock types and functions for testing
typedef void Canvas;
uint32_t furi_get_tick(void) { return 1000; }
void furi_log_print_format(int level, const char* tag, const char* format, ...) {}
void canvas_draw_dot(Canvas* canvas, int x, int y) {}

#include "raycaster.h"
#include "chunk_manager.h"
#include "terrain.h"

// Simple collision function for testing
bool test_collision(int16_t x, int16_t y, void* context) {
    (void)context;
    // Create a simple 3x3 terrain block at (65,32) to (67,34)
    return (x >= 65 && x <= 67 && y >= 32 && y <= 34);
}

int main() {
    printf("=== Debug Raycast Test ===\n");
    
    // Test basic raycasting from (64,32) with radius 4
    Raycaster* raycaster = raycaster_alloc();
    if (!raycaster) {
        printf("Failed to allocate raycaster\n");
        return 1;
    }
    
    // Test single ray going right from (64,32)
    RayDirection dir = {1000, 0, 0}; // dx=1000 (right), dy=0 (no vertical)
    RayResult result;
    
    printf("Testing ray from (64,32) going right with max distance 4\n");
    bool hit = raycaster_cast_ray(raycaster, 64, 32, dir, 4, &result, test_collision, NULL);
    
    printf("Result: hit=%s, hit_x=%d, hit_y=%d, distance=%d\n", 
           hit ? "true" : "false", result.hit_x, result.hit_y, result.distance);
    
    if (hit && result.hit_x >= 65 && result.hit_x <= 67) {
        printf("✅ SUCCESS: Ray correctly detected terrain at expected location\n");
    } else {
        printf("❌ FAILED: Ray did not detect terrain as expected\n");
    }
    
    // Test pattern casting
    RayPattern* pattern = raycaster_get_adaptive_pattern(raycaster, false);
    RayResult results[64];
    
    printf("\nTesting pattern with %d rays using new function\n", pattern->direction_count);
    uint16_t hits = raycaster_cast_pattern_with_radius(raycaster, pattern, 64, 32, 4, results, test_collision, NULL);
    
    printf("Pattern result: %d hits out of %d rays\n", hits, pattern->direction_count);
    
    // Show first few hit details
    int shown_hits = 0;
    for (int i = 0; i < pattern->direction_count && shown_hits < 5; i++) {
        if (results[i].hit_terrain && results[i].distance <= 4) {
            printf("  Ray %d: hit at (%d,%d) distance=%d\n", i, results[i].hit_x, results[i].hit_y, results[i].distance);
            shown_hits++;
        }
    }
    
    if (hits > 0) {
        printf("✅ SUCCESS: Pattern detected terrain\n");
    } else {
        printf("❌ FAILED: Pattern detected no terrain\n");
    }
    
    raycaster_free(raycaster);
    return 0;
}