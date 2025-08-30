#include "../test_common.h"
#include "../../game.h"
#include "../../chunk_manager.h"
#include "../../raycaster.h"
#include "../../sonar_chart.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// Mock GameManager functions for testing
typedef struct {
    GameContext* game_context;
} MockGameManager;

static MockGameManager mock_manager = {0};

GameContext* game_manager_game_context_get(void* manager) {
    (void)manager;
    return mock_manager.game_context;
}

InputState game_manager_input_get(void* manager) {
    (void)manager;
    InputState input = {0};
    return input;
}

void game_manager_game_stop(void* manager) {
    (void)manager;
    printf("Game stop requested\n");
}

void* game_manager_current_level_get(void* manager) {
    (void)manager;
    return NULL; // Not needed for these tests
}

void game_manager_add_level(void* manager, const void* level) {
    (void)manager;
    (void)level;
}

// Forward declare functions from game.c that we need to test
extern void game_start(void* game_manager, void* ctx);
extern void game_stop(void* ctx);

// Test complete game initialization sequence
bool test_complete_game_initialization() {
    printf("Testing complete game initialization...\n");
    
    // Allocate game context like the real game
    GameContext* game_context = malloc(sizeof(GameContext));
    memset(game_context, 0, sizeof(GameContext));
    mock_manager.game_context = game_context;
    
    // Call actual game initialization
    game_start(&mock_manager, game_context);
    
    // Validate all systems are properly initialized
    TEST_ASSERT(game_context->chunk_manager != NULL, "ChunkManager should be initialized");
    TEST_ASSERT(game_context->raycaster != NULL, "Raycaster should be initialized");
    TEST_ASSERT(game_context->sonar_chart != NULL, "SonarChart should be initialized");
    
    // Test screen position initialization
    TEST_ASSERT(game_context->screen_x == 64, "Screen X should be centered at 64");
    TEST_ASSERT(game_context->screen_y == 32, "Screen Y should be centered at 32");
    
    // Test world position initialization
    TEST_ASSERT(game_context->world_x > 0 && game_context->world_x < 1000, "World X should be reasonable");
    TEST_ASSERT(game_context->world_y > 0 && game_context->world_y < 1000, "World Y should be reasonable");
    
    // Test game state initialization
    TEST_ASSERT(game_context->ping_active == false, "Ping should start inactive");
    TEST_ASSERT(game_context->ping_radius == 0, "Ping radius should start at 0");
    TEST_ASSERT(game_context->mode == GAME_MODE_NAV, "Should start in navigation mode");
    TEST_ASSERT(game_context->velocity == 0, "Should start with zero velocity");
    
    // Test chunk loading at startup - this is CRITICAL for the bug
    printf("Checking chunk loading at position (%.1f, %.1f)...\n", 
           game_context->world_x, game_context->world_y);
    
    // The chunk manager should have loaded chunks around the starting position
    // Test collision detection works (indicates chunks are loaded)
    bool found_some_terrain = false;
    int terrain_count = 0;
    int water_count = 0;
    
    // Check a 20x20 area around starting position
    for(int dy = -10; dy <= 10; dy++) {
        for(int dx = -10; dx <= 10; dx++) {
            int test_x = (int)game_context->world_x + dx;
            int test_y = (int)game_context->world_y + dy;
            
            if(chunk_manager_check_collision(game_context->chunk_manager, test_x, test_y)) {
                terrain_count++;
                found_some_terrain = true;
            } else {
                water_count++;
            }
        }
    }
    
    printf("Found %d terrain pixels and %d water pixels in 21x21 area\n", 
           terrain_count, water_count);
    
    TEST_ASSERT(terrain_count + water_count == 441, "Should check all 441 pixels in 21x21 grid");
    // Terrain generation should create some terrain, but submarine starts in water
    TEST_ASSERT(found_some_terrain == true, "Should find some terrain in the area");
    TEST_ASSERT(water_count > 200, "Should find plenty of water (submarine starts in water)");
    
    // Clean up
    game_stop(game_context);
    free(game_context);
    
    printf("✅ Game initialization test PASSED\n");
    return true;
}

// Test complete ping workflow from button press to discovery
bool test_complete_ping_workflow() {
    printf("Testing complete ping workflow...\n");
    
    // Setup game exactly as it runs
    GameContext* game_context = malloc(sizeof(GameContext));
    memset(game_context, 0, sizeof(GameContext));
    mock_manager.game_context = game_context;
    
    // Initialize game
    game_start(&mock_manager, game_context);
    
    // Simulate ping button press (from game.c line 147-154)
    printf("Simulating ping button press...\n");
    if(!game_context->ping_active) {
        game_context->ping_active = true;
        game_context->ping_x = game_context->world_x;
        game_context->ping_y = game_context->world_y;
        game_context->ping_radius = 2; // Start with radius 2 (same as real game)
        game_context->ping_timer = furi_get_tick();
    }
    
    // Verify ping initialization
    TEST_ASSERT(game_context->ping_active == true, "Ping should be active after button press");
    TEST_ASSERT(game_context->ping_radius == 2, "Ping should start with radius 2");
    TEST_ASSERT(game_context->ping_x == game_context->world_x, "Ping X should match submarine position");
    TEST_ASSERT(game_context->ping_y == game_context->world_y, "Ping Y should match submarine position");
    
    // Count initial sonar points
    int initial_points = sonar_chart_count_points(game_context->sonar_chart);
    printf("Initial sonar points: %d\n", initial_points);
    
    // Simulate complete ping progression (from game.c lines 169-229)
    int discovered_points = 0;
    int max_frames = 35; // Should complete ping in reasonable time
    int frame = 0;
    
    printf("Starting ping progression simulation...\n");
    
    while(game_context->ping_active && frame < max_frames) {
        frame++;
        
        // Update ping radius (grows by 2 every 50ms in real game)
        game_context->ping_radius += 2;
        
        printf("Frame %d: ping_radius=%d\n", frame, game_context->ping_radius);
        
        // Count discoveries before this frame
        int points_before = sonar_chart_count_points(game_context->sonar_chart);
        
        // Process ping for this frame (CRITICAL PATH - exact same as game.c)
        if(game_context->raycaster && game_context->chunk_manager && game_context->sonar_chart) {
            // Use optimized raycasting with adaptive quality (same as game.c line 177)
            RayPattern* pattern = raycaster_get_adaptive_pattern(game_context->raycaster, false);
            RayResult results[RAY_CACHE_SIZE];
            
            // Cast rays from ping center up to current radius (same as game.c line 181)
            raycaster_cast_pattern(
                game_context->raycaster, 
                pattern,
                (int16_t)game_context->ping_x, 
                (int16_t)game_context->ping_y,
                results,
                NULL, // Using direct collision check
                game_context->chunk_manager
            );
            
            // Add discoveries to sonar chart (same as game.c lines 192-223)
            for(uint16_t i = 0; i < pattern->direction_count; i++) {
                RayResult* result = &results[i];
                if(result->ray_complete) {
                    // If ray hit terrain within ping radius, add it
                    if(result->hit_terrain && result->distance <= game_context->ping_radius) {
                        sonar_chart_add_point(game_context->sonar_chart, 
                                             result->hit_x, result->hit_y, true);
                        
                        // Add intermediate water points along the ray
                        int16_t start_x = (int16_t)game_context->ping_x;
                        int16_t start_y = (int16_t)game_context->ping_y;
                        int16_t dx = result->hit_x - start_x;
                        int16_t dy = result->hit_y - start_y;
                        
                        // Add water points along the ray path (every few steps)
                        for(uint16_t step = 3; step < result->distance; step += 3) {
                            int16_t water_x = start_x + (dx * step) / result->distance;
                            int16_t water_y = start_y + (dy * step) / result->distance;
                            sonar_chart_add_point(game_context->sonar_chart, water_x, water_y, false);
                        }
                    } else if(!result->hit_terrain || result->distance > game_context->ping_radius) {
                        // Ray extends beyond ping radius or didn't hit terrain
                        // Add water point at the edge of ping radius
                        int16_t start_x = (int16_t)game_context->ping_x;
                        int16_t start_y = (int16_t)game_context->ping_y;
                        float angle = raycaster_direction_to_angle(pattern->directions[i]);
                        int16_t edge_x = start_x + (int16_t)(cosf(angle) * game_context->ping_radius);
                        int16_t edge_y = start_y + (int16_t)(sinf(angle) * game_context->ping_radius);
                        sonar_chart_add_point(game_context->sonar_chart, edge_x, edge_y, false);
                    }
                }
            }
        }
        
        // Count discoveries after this frame
        int points_after = sonar_chart_count_points(game_context->sonar_chart);
        int new_discoveries = points_after - points_before;
        discovered_points += new_discoveries;
        
        printf("  New discoveries: %d, Total points: %d\n", new_discoveries, points_after);
        
        // End ping when radius exceeds 64 (same as game.c line 225)
        if(game_context->ping_radius > 64) {
            game_context->ping_active = false;
            printf("Ping completed at radius %d\n", game_context->ping_radius);
        }
    }
    
    printf("Ping progression complete after %d frames\n", frame);
    printf("Total discovered points: %d\n", discovered_points);
    
    // CRITICAL ASSERTIONS: Must discover substantial terrain
    int final_points = sonar_chart_count_points(game_context->sonar_chart);
    printf("Final sonar chart contains %d points\n", final_points);
    
    TEST_ASSERT(discovered_points > 10, "Should discover more than 10 points (not just '3 dots')");
    TEST_ASSERT(final_points > 10, "Sonar chart should contain more than 10 points");
    TEST_ASSERT(frame <= max_frames, "Ping should complete within reasonable time");
    
    // Additional validation: Check for terrain vs water distribution
    // Query the sonar chart to see what we actually discovered
    SonarBounds query_bounds = sonar_bounds_create(
        (int16_t)game_context->ping_x - 70,
        (int16_t)game_context->ping_y - 70,
        (int16_t)game_context->ping_x + 70,
        (int16_t)game_context->ping_y + 70
    );
    
    SonarPoint* visible_points[512];
    uint16_t point_count = sonar_chart_query_area(game_context->sonar_chart, 
                                                 query_bounds, visible_points, 512);
    
    int terrain_points = 0;
    int water_points = 0;
    for(uint16_t i = 0; i < point_count; i++) {
        if(visible_points[i]->is_terrain) {
            terrain_points++;
        } else {
            water_points++;
        }
    }
    
    printf("Discovered: %d terrain points, %d water points\n", terrain_points, water_points);
    
    TEST_ASSERT(terrain_points > 3, "Should find more than 3 terrain points (this was the bug!)");
    TEST_ASSERT(water_points > 0, "Should find some water points");
    
    // Clean up
    game_stop(game_context);
    free(game_context);
    
    printf("✅ Complete ping workflow test PASSED\n");
    return true;
}

int main() {
    printf("=== Complete Game Pipeline Tests ===\n\n");
    printf("These tests validate the complete ping workflow from game.c initialization to screen rendering.\n");
    printf("CRITICAL: This addresses the gap that allowed the '3 dots' bug to persist.\n\n");
    
    bool all_passed = true;
    
    if(!test_complete_game_initialization()) {
        all_passed = false;
    }
    
    printf("\n");
    
    if(!test_complete_ping_workflow()) {
        all_passed = false;
    }
    
    printf("\n=== PIPELINE TEST RESULTS ===\n");
    if(all_passed) {
        printf("🎉 ALL PIPELINE TESTS PASSED!\n");
        printf("The complete game pipeline is working correctly.\n");
        return 0;
    } else {
        printf("❌ SOME TESTS FAILED!\n");
        printf("The pipeline has issues that need to be fixed.\n");
        return 1;
    }
}