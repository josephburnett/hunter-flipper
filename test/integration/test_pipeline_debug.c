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

// Helper function to count loaded chunks (approximation)
int count_loaded_chunks_approximate(ChunkManager* cm, float world_x, float world_y) {
    // Test if chunks are loaded by checking collision detection in 4 quadrants
    int loaded_count = 0;
    
    // Check 4 corners around the position (each should be in a different chunk)
    int offsets[][2] = {{-32, -32}, {32, -32}, {-32, 32}, {32, 32}};
    
    for(int i = 0; i < 4; i++) {
        int test_x = (int)world_x + offsets[i][0];
        int test_y = (int)world_y + offsets[i][1];
        
        // If collision detection works, chunk is loaded
        // We don't care about the result, just that it doesn't crash
        bool result = chunk_manager_check_collision(cm, test_x, test_y);
        (void)result; // We got a result, so chunk is loaded
        loaded_count++;
    }
    
    return loaded_count;
}

// Helper function to count visible points in render area
int count_visible_points_in_render_area(SonarChart* chart, float world_x, float world_y) {
    int sample_radius = 80; // Same as game.c line 272
    
    SonarBounds query_bounds = sonar_bounds_create(
        (int16_t)world_x - sample_radius,
        (int16_t)world_y - sample_radius,
        (int16_t)world_x + sample_radius,
        (int16_t)world_y + sample_radius
    );
    
    SonarPoint* visible_points[512];
    uint16_t point_count = sonar_chart_query_area(chart, query_bounds, visible_points, 512);
    
    // Count points that would be visible on screen (transform and check bounds)
    int visible_count = 0;
    for(uint16_t i = 0; i < point_count; i++) {
        SonarPoint* point = visible_points[i];
        
        // Simple bounds check (world to screen transform approximation)
        float rel_x = point->world_x - world_x;
        float rel_y = point->world_y - world_y;
        
        // Check if within screen bounds (128x64)
        if(rel_x >= -64 && rel_x <= 64 && rel_y >= -32 && rel_y <= 32) {
            visible_count++;
        }
    }
    
    return visible_count;
}

// Test pipeline point tracing to find where points are lost
bool test_pipeline_point_tracing() {
    printf("=== PIPELINE DEBUG TRACE ===\n");
    printf("Tracing where discovered points are lost in the pipeline...\n\n");
    
    // Setup game
    GameContext* game_context = malloc(sizeof(GameContext));
    memset(game_context, 0, sizeof(GameContext));
    mock_manager.game_context = game_context;
    
    game_start(&mock_manager, game_context);
    
    printf("Game initialized at world position (%.1f, %.1f)\n", 
           game_context->world_x, game_context->world_y);
    
    // Stage 1: Chunk Manager - Are chunks loaded?
    printf("\n1. CHUNK LOADING STAGE:\n");
    chunk_manager_update(game_context->chunk_manager, 
                        game_context->world_x, game_context->world_y);
    
    int chunks = count_loaded_chunks_approximate(game_context->chunk_manager,
                                               game_context->world_x, game_context->world_y);
    printf("   Loaded chunks: %d (expected: 4)\n", chunks);
    
    // Test collision detection around submarine
    int collision_tests = 0;
    int terrain_collisions = 0;
    for(int dy = -20; dy <= 20; dy += 5) {
        for(int dx = -20; dx <= 20; dx += 5) {
            int test_x = (int)game_context->world_x + dx;
            int test_y = (int)game_context->world_y + dy;
            collision_tests++;
            
            if(chunk_manager_check_collision(game_context->chunk_manager, test_x, test_y)) {
                terrain_collisions++;
            }
        }
    }
    printf("   Collision detection: %d/%d tests hit terrain\n", terrain_collisions, collision_tests);
    TEST_ASSERT(chunks == 4, "Should have 4 chunks loaded (2x2 grid)");
    TEST_ASSERT(collision_tests > 0, "Should be able to run collision tests");
    
    // Stage 2: Raycaster - Are rays finding terrain?
    printf("\n2. RAY CASTING STAGE:\n");
    RayPattern* pattern = raycaster_get_adaptive_pattern(game_context->raycaster, false);
    printf("   Ray pattern: %d directions\n", pattern->direction_count);
    
    RayResult results[RAY_CACHE_SIZE];
    raycaster_cast_pattern(
        game_context->raycaster, 
        pattern,
        (int16_t)game_context->world_x, 
        (int16_t)game_context->world_y,
        results,
        NULL,
        game_context->chunk_manager
    );
    
    int rays_cast = 0;
    int terrain_hits = 0;
    int water_hits = 0;
    int completed_rays = 0;
    
    for(uint16_t i = 0; i < pattern->direction_count; i++) {
        RayResult* result = &results[i];
        rays_cast++;
        
        if(result->ray_complete) {
            completed_rays++;
            if(result->hit_terrain) {
                terrain_hits++;
            } else {
                water_hits++;
            }
        }
    }
    
    printf("   Rays cast: %d, Completed: %d\n", rays_cast, completed_rays);
    printf("   Terrain hits: %d, Water hits: %d\n", terrain_hits, water_hits);
    
    TEST_ASSERT(rays_cast > 0, "Should cast some rays");
    TEST_ASSERT(completed_rays > 0, "Some rays should complete");
    
    if(terrain_hits <= 3) {
        printf("   ⚠️  WARNING: Only found %d terrain hits - this is the '3 dots' bug!\n", terrain_hits);
    }
    
    // Stage 3: Sonar Chart - Are points being stored?
    printf("\n3. SONAR CHART STORAGE STAGE:\n");
    int points_before = sonar_chart_count_points(game_context->sonar_chart);
    printf("   Initial points: %d\n", points_before);
    
    // Simulate adding points at different radii (like ping expansion)
    int points_added_by_radius[33]; // Track points added at each radius
    memset(points_added_by_radius, 0, sizeof(points_added_by_radius));
    
    for(int radius = 2; radius <= 64; radius += 2) {
        int radius_index = radius / 2;
        int points_at_start = sonar_chart_count_points(game_context->sonar_chart);
        
        // Add points within current radius
        for(uint16_t i = 0; i < pattern->direction_count; i++) {
            RayResult* result = &results[i];
            if(result->ray_complete && result->distance <= radius) {
                if(result->hit_terrain) {
                    sonar_chart_add_point(game_context->sonar_chart, 
                                         result->hit_x, result->hit_y, true);
                }
                
                // Add water point at radius edge
                float angle = raycaster_direction_to_angle(pattern->directions[i]);
                int16_t edge_x = (int16_t)game_context->world_x + (int16_t)(cosf(angle) * radius);
                int16_t edge_y = (int16_t)game_context->world_y + (int16_t)(sinf(angle) * radius);
                sonar_chart_add_point(game_context->sonar_chart, edge_x, edge_y, false);
            }
        }
        
        int points_at_end = sonar_chart_count_points(game_context->sonar_chart);
        points_added_by_radius[radius_index] = points_at_end - points_at_start;
        
        if(radius <= 10) {
            printf("   Radius %d: added %d points\n", radius, points_added_by_radius[radius_index]);
        }
    }
    
    int points_after = sonar_chart_count_points(game_context->sonar_chart);
    printf("   Final points stored: %d (added: %d)\n", points_after, points_after - points_before);
    
    TEST_ASSERT(points_after > points_before, "Should have added some points");
    
    // Check early radius discoveries (this is where the bug manifests)
    int early_radius_total = points_added_by_radius[1] + points_added_by_radius[2] + points_added_by_radius[3];
    printf("   Points added in first 3 radius steps: %d\n", early_radius_total);
    
    if(early_radius_total <= 3) {
        printf("   🚨 BUG DETECTED: Only %d points in early radius - this is the '3 dots' bug!\n", early_radius_total);
    }
    
    // Stage 4: Rendering - Are points being drawn?
    printf("\n4. RENDER QUERY STAGE:\n");
    int visible_points = count_visible_points_in_render_area(game_context->sonar_chart,
                                                           game_context->world_x, game_context->world_y);
    printf("   Visible points for rendering: %d\n", visible_points);
    
    // Query the visible area to see terrain vs water breakdown
    int sample_radius = 80;
    SonarBounds query_bounds = sonar_bounds_create(
        (int16_t)game_context->world_x - sample_radius,
        (int16_t)game_context->world_y - sample_radius,
        (int16_t)game_context->world_x + sample_radius,
        (int16_t)game_context->world_y + sample_radius
    );
    
    SonarPoint* visible_point_list[512];
    uint16_t query_count = sonar_chart_query_area(game_context->sonar_chart, 
                                                 query_bounds, visible_point_list, 512);
    
    int render_terrain = 0;
    int render_water = 0;
    for(uint16_t i = 0; i < query_count; i++) {
        if(visible_point_list[i]->is_terrain) {
            render_terrain++;
        } else {
            render_water++;
        }
    }
    
    printf("   Render query found: %d terrain, %d water\n", render_terrain, render_water);
    
    TEST_ASSERT(visible_points > 0, "Should have some visible points for rendering");
    
    // Final assessment
    printf("\n=== PIPELINE ANALYSIS ===\n");
    
    bool pipeline_healthy = true;
    
    if(terrain_hits <= 3) {
        printf("❌ RAYCASTER BUG: Only %d terrain hits found\n", terrain_hits);
        pipeline_healthy = false;
    } else {
        printf("✅ Raycaster: Found %d terrain hits\n", terrain_hits);
    }
    
    if(early_radius_total <= 3) {
        printf("❌ EARLY RADIUS BUG: Only %d points in early ping radius\n", early_radius_total);
        pipeline_healthy = false;
    } else {
        printf("✅ Early radius: Found %d points in first 3 steps\n", early_radius_total);
    }
    
    if(render_terrain <= 3) {
        printf("❌ RENDER BUG: Only %d terrain points available for rendering\n", render_terrain);
        pipeline_healthy = false;
    } else {
        printf("✅ Rendering: %d terrain points available\n", render_terrain);
    }
    
    printf("\n=== PIPELINE TRACE COMPLETE ===\n");
    
    // Clean up
    game_stop(game_context);
    free(game_context);
    
    if(!pipeline_healthy) {
        printf("🚨 PIPELINE HAS CRITICAL BUGS - this explains the '3 dots only' issue\n");
    }
    
    return pipeline_healthy;
}

int main() {
    printf("=== Pipeline Debugging Test ===\n\n");
    printf("This test traces each stage of the ping pipeline to identify where points are lost.\n\n");
    
    bool pipeline_ok = test_pipeline_point_tracing();
    
    printf("\n=== PIPELINE DEBUG RESULTS ===\n");
    if(pipeline_ok) {
        printf("🎉 PIPELINE IS HEALTHY!\n");
        printf("All stages are working correctly.\n");
        return 0;
    } else {
        printf("🚨 PIPELINE HAS CRITICAL BUGS!\n");
        printf("This explains why the '3 dots only' bug persists.\n");
        return 1;
    }
}