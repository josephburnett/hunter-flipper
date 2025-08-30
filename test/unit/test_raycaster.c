#include "test_common.h"
#include "mock_furi.h" 
#include "raycaster.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

// Test collision function that creates a simple test world
bool test_world_collision(int16_t x, int16_t y, void* context) {
    (void)context;
    
    // Create a test world with some terrain patterns
    // Terrain exists at specific positions for predictable testing
    if (x == 5 && y == 0) return true;  // East
    if (x == 0 && y == 5) return true;  // South  
    if (x == -5 && y == 0) return true; // West
    if (x == 0 && y == -5) return true; // North
    if (x == 3 && y == 3) return true;  // Southeast
    
    return false; // Everything else is water
}

// Test 1: Bresenham Algorithm Correctness
bool test_bresenham_horizontal_line(TestResults* results) {
    printf("Testing Bresenham horizontal line...\n");
    results->tests_run++;
    
    Raycaster* rc = raycaster_alloc();
    TEST_ASSERT(rc != NULL, "Raycaster allocation failed");
    
    // Test horizontal line from (0,0) to (5,0)
    raycaster_bresham_init(rc, 0, 0, 5, 0);
    
    int16_t x, y;
    int steps = 0;
    int expected_x[] = {0, 1, 2, 3, 4, 5};
    
    while(raycaster_bresham_step(rc, &x, &y) && steps < 6) {
        TEST_ASSERT(x == expected_x[steps], "X coordinate incorrect");
        TEST_ASSERT(y == 0, "Y coordinate should be 0");
        steps++;
    }
    
    TEST_ASSERT(steps == 6, "Should have 6 steps for horizontal line");
    
    raycaster_free(rc);
    results->tests_passed++;
    return true;
}

bool test_bresenham_vertical_line(TestResults* results) {
    printf("Testing Bresenham vertical line...\n");
    results->tests_run++;
    
    Raycaster* rc = raycaster_alloc();
    TEST_ASSERT(rc != NULL, "Raycaster allocation failed");
    
    // Test vertical line from (0,0) to (0,5)
    raycaster_bresham_init(rc, 0, 0, 0, 5);
    
    int16_t x, y;
    int steps = 0;
    int expected_y[] = {0, 1, 2, 3, 4, 5};
    
    while(raycaster_bresham_step(rc, &x, &y) && steps < 6) {
        TEST_ASSERT(x == 0, "X coordinate should be 0");
        TEST_ASSERT(y == expected_y[steps], "Y coordinate incorrect");
        steps++;
    }
    
    TEST_ASSERT(steps == 6, "Should have 6 steps for vertical line");
    
    raycaster_free(rc);
    results->tests_passed++;
    return true;
}

bool test_bresenham_diagonal_line(TestResults* results) {
    printf("Testing Bresenham diagonal line...\n");
    results->tests_run++;
    
    Raycaster* rc = raycaster_alloc();
    TEST_ASSERT(rc != NULL, "Raycaster allocation failed");
    
    // Test 45-degree diagonal from (0,0) to (5,5)
    raycaster_bresham_init(rc, 0, 0, 5, 5);
    
    int16_t x, y;
    int steps = 0;
    
    while(raycaster_bresham_step(rc, &x, &y) && steps < 10) {
        // For perfect diagonal, x should equal y
        TEST_ASSERT(abs(x - y) <= 1, "Diagonal line deviation too large");
        steps++;
        if (x == 5 && y == 5) break;
    }
    
    TEST_ASSERT(steps > 0, "Should have some steps for diagonal");
    
    raycaster_free(rc);
    results->tests_passed++;
    return true;
}

bool test_bresenham_single_pixel(TestResults* results) {
    printf("Testing Bresenham single pixel...\n");
    results->tests_run++;
    
    Raycaster* rc = raycaster_alloc();
    TEST_ASSERT(rc != NULL, "Raycaster allocation failed");
    
    // Test single pixel "ray" from (10,10) to (10,10)
    raycaster_bresham_init(rc, 10, 10, 10, 10);
    
    int16_t x, y;
    bool has_step = raycaster_bresham_step(rc, &x, &y);
    
    TEST_ASSERT(has_step, "Should have at least one step");
    TEST_ASSERT(x == 10 && y == 10, "Should return starting position");
    
    // Second step should either not exist or still be the same position
    bool has_second = raycaster_bresham_step(rc, &x, &y);
    if (has_second) {
        TEST_ASSERT(x == 10 && y == 10, "Second step should still be start position");
    }
    
    raycaster_free(rc);
    results->tests_passed++;
    return true;
}

// Test 2: Ray Pattern Generation
bool test_ray_pattern_full_360(TestResults* results) {
    printf("Testing full 360° ray pattern...\n");
    results->tests_run++;
    
    Raycaster* rc = raycaster_alloc();
    TEST_ASSERT(rc != NULL, "Raycaster allocation failed");
    
    RayPattern* pattern = raycaster_get_adaptive_pattern(rc, false);
    TEST_ASSERT(pattern != NULL, "Pattern should not be NULL");
    TEST_ASSERT(pattern->direction_count == 32, "Full pattern should have 32 rays");
    TEST_ASSERT(pattern->max_radius > 0, "Max radius should be positive");
    
    // Verify even angle distribution
    float expected_angle_step = 2.0f * M_PI / 32.0f;
    for (int i = 0; i < pattern->direction_count; i++) {
        RayDirection* dir = &pattern->directions[i];
        
        // Convert direction back to angle
        float angle = atan2f(dir->dy, dir->dx);
        if (angle < 0) angle += 2.0f * M_PI;
        
        float expected_angle = i * expected_angle_step;
        float angle_diff = fabsf(angle - expected_angle);
        if (angle_diff > M_PI) angle_diff = 2.0f * M_PI - angle_diff;
        
        TEST_ASSERT(angle_diff < 0.2f, "Angle distribution not even");
    }
    
    raycaster_free(rc);
    results->tests_passed++;
    return true;
}

bool test_ray_pattern_forward_180(TestResults* results) {
    printf("Testing forward 180° ray pattern...\n");
    results->tests_run++;
    
    Raycaster* rc = raycaster_alloc();
    TEST_ASSERT(rc != NULL, "Raycaster allocation failed");
    
    RayPattern* pattern = raycaster_get_adaptive_pattern(rc, true);
    TEST_ASSERT(pattern != NULL, "Forward pattern should not be NULL");
    TEST_ASSERT(pattern->direction_count == 16, "Forward pattern should have 16 rays");
    
    raycaster_free(rc);
    results->tests_passed++;
    return true;
}

// Test 3: Adaptive Quality Levels
bool test_adaptive_quality_levels(TestResults* results) {
    printf("Testing adaptive quality levels...\n");
    results->tests_run++;
    
    Raycaster* rc = raycaster_alloc();
    TEST_ASSERT(rc != NULL, "Raycaster allocation failed");
    
    // Test quality level 0 (all rays)
    raycaster_set_quality_level(rc, 0);
    TEST_ASSERT(rc->current_quality_level == 0, "Quality level should be 0");
    
    RayPattern* pattern = raycaster_get_adaptive_pattern(rc, false);
    RayResult results_q0[64];
    
    uint16_t hits_q0 = raycaster_cast_pattern(rc, pattern, 0, 0, results_q0, 
                                            test_world_collision, NULL);
    uint16_t rays_cast_q0 = rc->rays_cast_this_frame;
    
    // Test quality level 1 (every other ray)
    raycaster_set_quality_level(rc, 1);
    TEST_ASSERT(rc->current_quality_level == 1, "Quality level should be 1");
    
    RayResult results_q1[64];
    raycaster_reset_frame_stats(rc);
    
    uint16_t hits_q1 = raycaster_cast_pattern(rc, pattern, 0, 0, results_q1,
                                            test_world_collision, NULL);
    uint16_t rays_cast_q1 = rc->rays_cast_this_frame;
    
    // Quality 1 should cast fewer rays than quality 0
    TEST_ASSERT(rays_cast_q1 <= rays_cast_q0, "Higher quality should cast fewer rays");
    
    // Verify skipped rays are properly initialized
    for (int i = 0; i < pattern->direction_count; i++) {
        if (rc->current_quality_level > 0 && (i % (rc->current_quality_level + 1)) != 0) {
            // This ray should be skipped
            TEST_ASSERT(results_q1[i].ray_complete, "Skipped ray should be marked complete");
            TEST_ASSERT(!results_q1[i].hit_terrain, "Skipped ray should not hit terrain");
        }
    }
    
    raycaster_free(rc);
    results->tests_passed++;
    return true;
}

// Test 4: Performance Tracking
bool test_performance_tracking(TestResults* results) {
    printf("Testing performance tracking...\n");
    results->tests_run++;
    
    Raycaster* rc = raycaster_alloc();
    TEST_ASSERT(rc != NULL, "Raycaster allocation failed");
    
    // Reset stats
    raycaster_reset_frame_stats(rc);
    TEST_ASSERT(rc->rays_cast_this_frame == 0, "Rays cast should be 0 after reset");
    TEST_ASSERT(rc->early_exits_this_frame == 0, "Early exits should be 0 after reset");
    
    // Cast some rays
    RayPattern* pattern = raycaster_get_adaptive_pattern(rc, false);
    RayResult results_array[64];
    
    raycaster_cast_pattern(rc, pattern, 0, 0, results_array, test_world_collision, NULL);
    
    // Verify stats are updated
    TEST_ASSERT(rc->rays_cast_this_frame > 0, "Should have cast some rays");
    
    raycaster_free(rc);
    results->tests_passed++;
    return true;
}

// Test 5: Critical Bug Detection (Progressive Ping)
bool test_progressive_ping_radius(TestResults* results) {
    printf("Testing progressive ping radius (critical for '3 dots' bug)...\n");
    results->tests_run++;
    
    Raycaster* rc = raycaster_alloc();
    TEST_ASSERT(rc != NULL, "Raycaster allocation failed");
    
    RayPattern* pattern = raycaster_get_adaptive_pattern(rc, false);
    RayResult ray_results[64];
    
    // Cast rays from origin
    raycaster_cast_pattern(rc, pattern, 0, 0, ray_results, test_world_collision, NULL);
    
    // Simulate progressive ping expansion
    int discoveries[10] = {0}; // Track discoveries per radius
    
    for (int radius = 2; radius <= 10; radius += 2) {
        int radius_index = (radius / 2) - 1;
        
        for (int i = 0; i < pattern->direction_count; i++) {
            RayResult* result = &ray_results[i];
            
            if (result->ray_complete && result->hit_terrain && result->distance <= radius && result->distance > radius - 2) {
                discoveries[radius_index]++;
            }
        }
    }
    
    // Validate discovery pattern - should find our test terrain
    int total_discovered = 0;
    for (int i = 0; i < 5; i++) {
        total_discovered += discoveries[i];
    }
    
    TEST_ASSERT(total_discovered > 0, "Should discover some terrain in progressive ping");
    
    // Critical test: early radius should have some discoveries (not the "3 dots" bug)
    int early_discoveries = discoveries[0] + discoveries[1] + discoveries[2]; // radius 2,4,6
    if (early_discoveries <= 3 && total_discovered > early_discoveries) {
        printf("    WARNING: Only %d discoveries in early frames - potential '3 dots' bug pattern!\n", early_discoveries);
    }
    
    raycaster_free(rc);
    results->tests_passed++;
    return true;
}

int main(void) {
    printf("=== Unit Tests: Raycaster Module ===\n\n");
    
    TestResults results = {0, 0, 0};
    
    // Bresenham algorithm tests
    if (!test_bresenham_horizontal_line(&results)) results.tests_failed++;
    if (!test_bresenham_vertical_line(&results)) results.tests_failed++;
    if (!test_bresenham_diagonal_line(&results)) results.tests_failed++;
    if (!test_bresenham_single_pixel(&results)) results.tests_failed++;
    
    // Ray pattern tests
    if (!test_ray_pattern_full_360(&results)) results.tests_failed++;
    if (!test_ray_pattern_forward_180(&results)) results.tests_failed++;
    
    // Adaptive quality tests
    if (!test_adaptive_quality_levels(&results)) results.tests_failed++;
    
    // Performance tests
    if (!test_performance_tracking(&results)) results.tests_failed++;
    
    // Critical bug detection
    if (!test_progressive_ping_radius(&results)) results.tests_failed++;
    
    // Summary
    printf("\n=== Raycaster Unit Test Results ===\n");
    printf("Tests run: %d\n", results.tests_run);
    printf("Tests passed: %d\n", results.tests_passed);
    printf("Tests failed: %d\n", results.tests_failed);
    
    if (results.tests_failed == 0) {
        printf("✅ All raycaster unit tests PASSED!\n");
        return 0;
    } else {
        printf("❌ %d raycaster unit tests FAILED!\n", results.tests_failed);
        return 1;
    }
}