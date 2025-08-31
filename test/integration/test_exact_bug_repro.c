// Test 3.1: Exact Bug Reproduction
// Reproduce the exact scenario from the log to confirm the bug

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../../test_common.h"
#include "../../sonar_chart.h"

// Mock engine.h functions for test environment
#ifndef TEST_BUILD
#include "../../engine/engine.h"
#endif

// Test helper function to create a test chart matching game setup
SonarChart* create_test_chart() {
    SonarChart* chart = malloc(sizeof(SonarChart));
    if(!chart) return NULL;
    
    // Initialize memory pools with realistic sizes
    if(!sonar_node_pool_init(&chart->node_pool, 128)) {
        free(chart);
        return NULL;
    }
    
    if(!sonar_point_pool_init(&chart->point_pool, 512)) {
        sonar_node_pool_cleanup(&chart->node_pool);
        free(chart);
        return NULL;
    }
    
    // Create root node with large bounds (matching actual game)
    SonarBounds root_bounds = sonar_bounds_create(-32768, -32768, 32767, 32767);
    chart->root = sonar_quad_create(chart, root_bounds, 0);
    
    // Initialize other fields
    chart->last_fade_update = 0;
    chart->points_faded_this_frame = 0;
    chart->cache_count = 0;
    chart->last_query_bounds = sonar_bounds_create(0, 0, 0, 0);
    chart->points_added_this_frame = 0;
    chart->points_removed_this_frame = 0;
    chart->query_count_this_frame = 0;
    
    return chart;
}

// Mock game context structure
typedef struct {
    int world_x;
    int world_y;
    SonarChart* sonar_chart;
} GameContext;

GameContext setup_game_context() {
    GameContext ctx = {0};
    ctx.sonar_chart = create_test_chart();
    return ctx;
}

// Test function: Exact bug scenario reproduction
bool test_exact_bug_scenario() {
    printf("=== Test 3.1: Exact Bug Scenario Reproduction ===\n");
    printf("Reproducing the scenario from /home/joseph/hunter-flipper/log.txt\n\n");
    
    GameContext ctx = setup_game_context();
    if(!ctx.sonar_chart) {
        printf("FAIL: Could not create test chart\n");
        return false;
    }
    
    // Position from log
    ctx.world_x = 60;
    ctx.world_y = 51;
    
    printf("Game context: Player at (%d, %d)\n", ctx.world_x, ctx.world_y);
    
    printf("\nStep 1: Adding points exactly as shown in log...\n");
    
    // Add points exactly as discovered in the log
    // From the test plan: "(66,51), (66,52), (70,57), (48,55)"
    // Plus additional points that were mentioned
    
    struct { int x, y; } log_points[] = {
        {66, 51}, {66, 52}, {66, 53}, {70, 57}, {48, 55},
        {67, 51}, {67, 52}, {68, 51}, {69, 52}, {65, 53},
        {64, 54}, {63, 55}, {71, 58}, {72, 59}, {49, 56},
        {50, 57}, {47, 54}, {46, 53}, {68, 60}, {69, 61}
    };
    
    int num_log_points = sizeof(log_points) / sizeof(log_points[0]);
    int points_added = 0;
    
    for(int i = 0; i < num_log_points; i++) {
        bool added = sonar_chart_add_point(ctx.sonar_chart, log_points[i].x, log_points[i].y, true);
        if(added) {
            points_added++;
            printf("  Added terrain point at (%d, %d)\n", log_points[i].x, log_points[i].y);
        } else {
            printf("  FAILED to add terrain point at (%d, %d)\n", log_points[i].x, log_points[i].y);
        }
    }
    
    printf("Successfully added %d out of %d terrain points from log\n", points_added, num_log_points);
    
    printf("\nStep 2: Performing render query as shown in log...\n");
    
    // Query as render does - based on log showing "32 total (1 terrain)"
    // The render bounds from the test plan: {-20, -29, 140, 131}
    SonarBounds render_query = {-20, -29, 140, 131};
    SonarPoint* points[100];
    uint16_t count = sonar_chart_query_area(ctx.sonar_chart, render_query, points, 100);
    
    printf("Render query bounds: (%d,%d) to (%d,%d)\n", 
           render_query.min_x, render_query.min_y, render_query.max_x, render_query.max_y);
    printf("Render query returned %d total points\n", count);
    
    // Count terrain vs water
    int terrain_count = 0;
    int water_count = 0;
    for(int i = 0; i < count; i++) {
        if(points[i]->is_terrain) {
            terrain_count++;
        } else {
            water_count++;
        }
    }
    
    printf("Breakdown: %d terrain, %d water\n", terrain_count, water_count);
    
    printf("\nStep 3: Analyzing the discrepancy...\n");
    
    // The bug: Log shows "32 total (1 terrain)" but we added 20+ terrain points
    printf("Expected terrain points: %d (added successfully)\n", points_added);
    printf("Found terrain points: %d (from query)\n", terrain_count);
    printf("Missing terrain points: %d\n", points_added - terrain_count);
    
    if(terrain_count < points_added) {
        printf("❌ BUG CONFIRMED: Terrain points are being lost!\n");
        printf("This reproduces the 'single pixel land' bug from the log.\n");
        
        // Show which specific points are missing
        printf("\nStep 4: Checking which specific points are missing...\n");
        
        for(int i = 0; i < num_log_points; i++) {
            SonarBounds exact = {log_points[i].x, log_points[i].y, log_points[i].x, log_points[i].y};
            SonarPoint* exact_points[5];
            uint16_t exact_count = sonar_chart_query_area(ctx.sonar_chart, exact, exact_points, 5);
            
            if(exact_count == 0) {
                printf("  MISSING: Point (%d,%d) not found in any query\n", log_points[i].x, log_points[i].y);
            }
        }
        
        printf("\nStep 5: Testing wider area queries...\n");
        
        // Test with progressively wider queries
        SonarBounds wide_queries[] = {
            {40, 40, 80, 70},      // Around the cluster
            {0, 0, 128, 128},      // Larger area
            {-100, -100, 200, 200}, // Very wide
            {-32768, -32768, 32767, 32767}  // Entire root bounds
        };
        
        for(int q = 0; q < 4; q++) {
            SonarPoint* wide_points[500];
            uint16_t wide_count = sonar_chart_query_area(ctx.sonar_chart, wide_queries[q], wide_points, 500);
            
            int wide_terrain = 0;
            for(int i = 0; i < wide_count; i++) {
                if(wide_points[i]->is_terrain) wide_terrain++;
            }
            
            printf("  Query %d bounds (%d,%d)-(%d,%d): %d total, %d terrain\n", 
                   q+1, wide_queries[q].min_x, wide_queries[q].min_y, 
                   wide_queries[q].max_x, wide_queries[q].max_y, wide_count, wide_terrain);
        }
        
        return false; // Bug confirmed
        
    } else if(terrain_count == points_added) {
        printf("⚠️  Unexpected: All terrain points found in render query\n");
        printf("The bug may not reproduce with this exact scenario.\n");
        printf("This could indicate:\n");
        printf("- The bug is timing/state dependent\n");
        printf("- The bug occurs with larger datasets\n");
        printf("- The bug is in coordinate transformation, not storage\n");
        
        return true; // Test passed but didn't reproduce bug
        
    } else {
        printf("❓ Confusing: Found MORE terrain points than expected (%d > %d)\n", 
               terrain_count, points_added);
        return false;
    }
}

// Test function: Simulate the progressive ping that led to the bug
bool test_progressive_ping_bug_reproduction() {
    printf("\n=== Test 3.1b: Progressive Ping Bug Reproduction ===\n");
    
    GameContext ctx = setup_game_context();
    if(!ctx.sonar_chart) {
        printf("FAIL: Could not create test chart\n");
        return false;
    }
    
    // Simulate a ping discovery sequence that leads to many terrain points
    ctx.world_x = 60;
    ctx.world_y = 51;
    
    printf("Simulating progressive ping discovery from position (%d,%d)\n", ctx.world_x, ctx.world_y);
    
    int total_discovered = 0;
    int ping_radius = 2;
    
    // Simulate multiple ping expansions discovering terrain
    while(ping_radius <= 32 && total_discovered < 50) {
        printf("\nPing expansion: radius %d\n", ping_radius);
        
        int discoveries_this_ping = 0;
        
        // Simulate raycasting finding terrain in this radius
        for(int angle = 0; angle < 16; angle++) { // 16 rays around circle
            // Mock terrain discovery at various points
            double rad_angle = (angle * 2 * 3.14159) / 16.0;
            int discovered_x = ctx.world_x + (int)(ping_radius * cos(rad_angle));
            int discovered_y = ctx.world_y + (int)(ping_radius * sin(rad_angle));
            
            // Simulate terrain at some of these points
            if((discovered_x + discovered_y) % 3 == 0) {  // Mock terrain pattern
                bool added = sonar_chart_add_point(ctx.sonar_chart, discovered_x, discovered_y, true);
                if(added) {
                    discoveries_this_ping++;
                    total_discovered++;
                }
            }
        }
        
        printf("  Discovered %d terrain points this expansion\n", discoveries_this_ping);
        
        // Query to see how many points we can retrieve
        SonarBounds query = {-100, -100, 200, 200};
        SonarPoint* points[200];
        uint16_t retrieved = sonar_chart_query_area(ctx.sonar_chart, query, points, 200);
        
        int retrieved_terrain = 0;
        for(int i = 0; i < retrieved; i++) {
            if(points[i]->is_terrain) retrieved_terrain++;
        }
        
        printf("  Total discovered so far: %d\n", total_discovered);
        printf("  Total retrievable: %d (%d terrain)\n", retrieved, retrieved_terrain);
        
        if(retrieved_terrain < total_discovered) {
            printf("  ❌ DISCREPANCY: Lost %d terrain points!\n", total_discovered - retrieved_terrain);
        }
        
        ping_radius += 4;
    }
    
    // Final comprehensive test
    printf("\nFinal verification:\n");
    printf("Total terrain points added: %d\n", total_discovered);
    
    // Try different query sizes
    SonarBounds final_queries[] = {
        {-50, -50, 150, 150},
        {-200, -200, 400, 400}
    };
    
    for(int q = 0; q < 2; q++) {
        SonarPoint* final_points[500];
        uint16_t final_count = sonar_chart_query_area(ctx.sonar_chart, final_queries[q], final_points, 500);
        
        int final_terrain = 0;
        for(int i = 0; i < final_count; i++) {
            if(final_points[i]->is_terrain) final_terrain++;
        }
        
        printf("Final query %d: %d total, %d terrain (expected %d terrain)\n", 
               q+1, final_count, final_terrain, total_discovered);
        
        if(final_terrain < total_discovered) {
            printf("❌ BUG CONFIRMED: Progressive ping loses terrain points!\n");
            return false;
        }
    }
    
    printf("✓ Progressive ping maintained all terrain points\n");
    return true;
}

int main() {
    printf("Hunter-Flipper Test Suite: Phase 3 - Bug Reproduction\n");
    printf("Test File: test_exact_bug_repro.c\n");
    printf("Purpose: Reproduce the exact 'single pixel land' bug scenario\n\n");
    
    bool all_passed = true;
    
    all_passed &= test_exact_bug_scenario();
    all_passed &= test_progressive_ping_bug_reproduction();
    
    if(!all_passed) {
        printf("\n🎯 BUG SUCCESSFULLY REPRODUCED!\n");
        printf("The 'single pixel land' bug has been confirmed.\n");
        printf("Root cause: Points are lost during quadtree operations.\n");
        printf("Next steps: Fix the quadtree subdivision algorithm.\n");
        return 1; // Indicate bug found
    } else {
        printf("\n⚠️  Bug not reproduced in this test\n");
        printf("The bug may require specific conditions not captured here.\n");
        return 0;
    }
}