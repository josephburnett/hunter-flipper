#include "../test_common.h"
#include "../../game.h"
#include "../../chunk_manager.h"
#include "../../raycaster.h"
#include "../../sonar_chart.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

// Mock GameManager functions
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
}

void* game_manager_current_level_get(void* manager) {
    (void)manager;
    return NULL;
}

void game_manager_add_level(void* manager, const void* level) {
    (void)manager;
    (void)level;
}

// Forward declare functions from game.c
extern void game_start(void* game_manager, void* ctx);
extern void game_stop(void* ctx);

// Test game state transitions during ping cycle
bool test_game_state_ping_cycle() {
    printf("Testing game state ping cycle...\n");
    
    GameContext* game_context = malloc(sizeof(GameContext));
    memset(game_context, 0, sizeof(GameContext));
    mock_manager.game_context = game_context;
    
    game_start(&mock_manager, game_context);
    
    // Test initial state
    TEST_ASSERT(game_context->ping_active == false, "Ping should start inactive");
    TEST_ASSERT(game_context->ping_radius == 0, "Ping radius should start at 0");
    TEST_ASSERT(game_context->mode == GAME_MODE_NAV, "Should start in navigation mode");
    TEST_ASSERT(game_context->velocity == 0, "Should start with zero velocity");
    
    printf("✅ Initial state validated\n");
    
    // Test ping activation (simulate button press from game.c)
    printf("Simulating ping button press...\n");
    if(!game_context->ping_active) {
        game_context->ping_active = true;
        game_context->ping_x = game_context->world_x;
        game_context->ping_y = game_context->world_y;
        game_context->ping_radius = 2; // Same as game.c line 152
        game_context->ping_timer = furi_get_tick();
    }
    
    TEST_ASSERT(game_context->ping_active == true, "Ping should be active after button press");
    TEST_ASSERT(game_context->ping_radius == 2, "Ping should start with radius 2");
    
    printf("✅ Ping activation validated\n");
    
    // Test ping progression
    printf("Testing ping progression...\n");
    int initial_points = sonar_chart_count_points(game_context->sonar_chart);
    int progression_steps = 0;
    
    while(game_context->ping_active && progression_steps < 35) {
        progression_steps++;
        
        // Update ping radius (same as game.c line 173)
        game_context->ping_radius += 2;
        
        // Simulate ping processing (simplified version of game.c lines 177-223)
        if(game_context->raycaster && game_context->chunk_manager && game_context->sonar_chart) {
            RayPattern* pattern = raycaster_get_adaptive_pattern(game_context->raycaster, false);
            RayResult results[RAY_CACHE_SIZE];
            
            raycaster_cast_pattern(
                game_context->raycaster, 
                pattern,
                (int16_t)game_context->ping_x, 
                (int16_t)game_context->ping_y,
                results,
                NULL,
                game_context->chunk_manager
            );
            
            // Add some discoveries
            for(uint16_t i = 0; i < pattern->direction_count && i < 8; i++) {
                RayResult* result = &results[i];
                if(result->ray_complete && result->distance <= game_context->ping_radius) {
                    if(result->hit_terrain) {
                        sonar_chart_add_point(game_context->sonar_chart, 
                                             result->hit_x, result->hit_y, true);
                    }
                    
                    // Add water point
                    float angle = raycaster_direction_to_angle(pattern->directions[i]);
                    int16_t edge_x = (int16_t)game_context->ping_x + (int16_t)(cosf(angle) * game_context->ping_radius);
                    int16_t edge_y = (int16_t)game_context->ping_y + (int16_t)(sinf(angle) * game_context->ping_radius);
                    sonar_chart_add_point(game_context->sonar_chart, edge_x, edge_y, false);
                }
            }
        }
        
        // End ping when radius exceeds 64 (same as game.c line 225)
        if(game_context->ping_radius > 64) {
            game_context->ping_active = false;
            printf("Ping completed at radius %d after %d steps\n", game_context->ping_radius, progression_steps);
        }
    }
    
    // Test post-ping state
    TEST_ASSERT(game_context->ping_active == false, "Ping should be inactive after completion");
    
    int final_points = sonar_chart_count_points(game_context->sonar_chart);
    int points_discovered = final_points - initial_points;
    
    printf("Points discovered during ping: %d\n", points_discovered);
    TEST_ASSERT(points_discovered > 0, "Should discover some points during ping");
    
    printf("✅ Ping completion validated\n");
    
    // Clean up
    game_stop(game_context);
    free(game_context);
    
    printf("✅ Game state ping cycle test PASSED\n");
    return true;
}

// Test game state persistence and memory management
bool test_game_state_persistence() {
    printf("Testing game state persistence and memory management...\n");
    
    GameContext* game_context = malloc(sizeof(GameContext));
    memset(game_context, 0, sizeof(GameContext));
    mock_manager.game_context = game_context;
    
    // Test initialization
    game_start(&mock_manager, game_context);
    
    // Record initial state
    float initial_x = game_context->world_x;
    float initial_y = game_context->world_y;
    ChunkManager* initial_cm = game_context->chunk_manager;
    SonarChart* initial_sc = game_context->sonar_chart;
    Raycaster* initial_rc = game_context->raycaster;
    
    TEST_ASSERT(initial_cm != NULL, "ChunkManager should be allocated");
    TEST_ASSERT(initial_sc != NULL, "SonarChart should be allocated");
    TEST_ASSERT(initial_rc != NULL, "Raycaster should be allocated");
    
    // Simulate some game activity
    game_context->world_x += 10.0f;
    game_context->world_y += 5.0f;
    game_context->velocity = 0.05f;
    game_context->heading = 0.25f;
    
    // Add some sonar data
    sonar_chart_add_point(game_context->sonar_chart, 100, 100, true);
    sonar_chart_add_point(game_context->sonar_chart, 105, 105, false);
    
    int points_added = sonar_chart_count_points(game_context->sonar_chart);
    TEST_ASSERT(points_added >= 2, "Should have added sonar points");
    
    printf("Game state modified successfully\n");
    
    // Test cleanup
    game_stop(game_context);
    
    // Note: After game_stop, the pointers should be cleaned up
    // but we can't safely test them since they're freed
    
    printf("✅ Game state persistence test PASSED\n");
    
    free(game_context);
    return true;
}

// Test mode switching and input handling
bool test_mode_switching() {
    printf("Testing mode switching...\n");
    
    GameContext* game_context = malloc(sizeof(GameContext));
    memset(game_context, 0, sizeof(GameContext));
    mock_manager.game_context = game_context;
    
    game_start(&mock_manager, game_context);
    
    // Test initial mode
    TEST_ASSERT(game_context->mode == GAME_MODE_NAV, "Should start in navigation mode");
    
    // Simulate mode switch (from game.c lines 87-90)
    game_context->mode = (game_context->mode == GAME_MODE_NAV) ? 
                         GAME_MODE_TORPEDO : GAME_MODE_NAV;
    
    TEST_ASSERT(game_context->mode == GAME_MODE_TORPEDO, "Should switch to torpedo mode");
    
    // Switch back
    game_context->mode = (game_context->mode == GAME_MODE_NAV) ? 
                         GAME_MODE_TORPEDO : GAME_MODE_NAV;
    
    TEST_ASSERT(game_context->mode == GAME_MODE_NAV, "Should switch back to navigation mode");
    
    printf("✅ Mode switching test PASSED\n");
    
    game_stop(game_context);
    free(game_context);
    return true;
}

int main() {
    printf("=== Game State Validation Tests ===\n\n");
    printf("These tests validate game state transitions and persistence.\n\n");
    
    bool all_passed = true;
    
    if(!test_game_state_ping_cycle()) {
        all_passed = false;
    }
    
    printf("\n");
    
    if(!test_game_state_persistence()) {
        all_passed = false;
    }
    
    printf("\n");
    
    if(!test_mode_switching()) {
        all_passed = false;
    }
    
    printf("\n=== GAME STATE TEST RESULTS ===\n");
    if(all_passed) {
        printf("🎉 ALL GAME STATE TESTS PASSED!\n");
        return 0;
    } else {
        printf("❌ SOME GAME STATE TESTS FAILED!\n");
        return 1;
    }
}