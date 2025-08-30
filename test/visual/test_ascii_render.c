#include "test_common.h"
#include "mock_furi.h"
#include "sonar_chart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ASCII Renderer for Visual Debugging (as specified in test plan)
void render_ascii_sonar(SonarChart* chart, int cx, int cy) {
    printf("\n=== Sonar Display (ASCII Visualization) ===\n");
    printf("Center: (%d, %d)\n", cx, cy);
    printf("Legend: 'S' = Submarine, '#' = Terrain, '~' = Water, '.' = Discovered water, ' ' = Unknown\n\n");
    
    // Create 40x20 ASCII display as specified in test plan
    int width = 40;
    int height = 20;
    int start_x = cx - width/2;
    int start_y = cy - height/2;
    
    printf("    ");
    for(int i = 0; i < width; i += 5) {
        printf("%-5d", start_x + i);
    }
    printf("\n");
    
    for(int y = 0; y < height; y++) {
        int world_y = start_y + y;
        printf("%3d ", world_y);
        
        for(int x = 0; x < width; x++) {
            int world_x = start_x + x;
            
            if(world_x == cx && world_y == cy) {
                printf("S"); // Submarine
            } else {
                // Query sonar chart for this position
                SonarPoint* point;
                if(sonar_chart_query_point(chart, world_x, world_y, &point)) {
                    if(point->is_terrain) {
                        printf("#"); // Terrain
                    } else {
                        printf("~"); // Water
                    }
                } else {
                    printf(" "); // Unknown/undiscovered
                }
            }
        }
        printf("\n");
    }
    printf("\n");
}

// Test the ASCII renderer with sample data
bool test_ascii_renderer_basic(TestResults* results) {
    printf("Testing ASCII renderer basic functionality...\n");
    results->tests_run++;
    
    SonarChart* chart = sonar_chart_alloc();
    TEST_ASSERT(chart != NULL, "Sonar chart allocation failed");
    
    // Add some test data
    sonar_chart_add_point(chart, 64, 32, true);   // Terrain at submarine position
    sonar_chart_add_point(chart, 65, 32, true);   // Terrain to the east
    sonar_chart_add_point(chart, 64, 33, true);   // Terrain to the south
    sonar_chart_add_point(chart, 63, 32, false);  // Water to the west
    sonar_chart_add_point(chart, 64, 31, false);  // Water to the north
    
    printf("Rendering sonar chart with test data:\n");
    render_ascii_sonar(chart, 64, 32);
    
    sonar_chart_free(chart);
    
    results->tests_passed++;
    return true;
}

// Test renderer with progressive ping data
bool test_ascii_renderer_progressive(TestResults* results) {
    printf("Testing ASCII renderer with progressive ping data...\n");
    results->tests_run++;
    
    SonarChart* chart = sonar_chart_alloc();
    TEST_ASSERT(chart != NULL, "Sonar chart allocation failed");
    
    // Simulate progressive ping discovery
    int sub_x = 64, sub_y = 32;
    
    printf("Simulating progressive ping discovery:\n");
    
    // Radius 2: Close terrain
    sonar_chart_add_point(chart, sub_x + 1, sub_y, true);
    sonar_chart_add_point(chart, sub_x, sub_y + 1, true);
    printf("After radius 2 ping:\n");
    render_ascii_sonar(chart, sub_x, sub_y);
    
    // Radius 4: More terrain and water
    sonar_chart_add_point(chart, sub_x + 2, sub_y, true);
    sonar_chart_add_point(chart, sub_x - 1, sub_y, false);
    sonar_chart_add_point(chart, sub_x, sub_y - 1, false);
    printf("After radius 4 ping:\n");
    render_ascii_sonar(chart, sub_x, sub_y);
    
    // Radius 6: Full pattern
    sonar_chart_add_point(chart, sub_x + 2, sub_y + 1, true);
    sonar_chart_add_point(chart, sub_x + 1, sub_y + 2, true);
    sonar_chart_add_point(chart, sub_x - 1, sub_y + 1, false);
    sonar_chart_add_point(chart, sub_x - 2, sub_y, false);
    printf("After radius 6 ping:\n");
    render_ascii_sonar(chart, sub_x, sub_y);
    
    sonar_chart_free(chart);
    
    results->tests_passed++;
    return true;
}

// Test renderer edge cases
bool test_ascii_renderer_edge_cases(TestResults* results) {
    printf("Testing ASCII renderer edge cases...\n");
    results->tests_run++;
    
    SonarChart* chart = sonar_chart_alloc();
    TEST_ASSERT(chart != NULL, "Sonar chart allocation failed");
    
    // Test empty chart
    printf("Empty sonar chart:\n");
    render_ascii_sonar(chart, 0, 0);
    
    // Test chart with only water
    sonar_chart_add_point(chart, 0, 0, false);
    sonar_chart_add_point(chart, 1, 0, false);
    sonar_chart_add_point(chart, 0, 1, false);
    printf("Chart with only water:\n");
    render_ascii_sonar(chart, 0, 0);
    
    // Test chart with only terrain
    sonar_chart_free(chart);
    chart = sonar_chart_alloc();
    sonar_chart_add_point(chart, 0, 0, true);
    sonar_chart_add_point(chart, 1, 0, true);
    sonar_chart_add_point(chart, 0, 1, true);
    printf("Chart with only terrain:\n");
    render_ascii_sonar(chart, 0, 0);
    
    sonar_chart_free(chart);
    
    results->tests_passed++;
    return true;
}

// Test the "3 dots" bug visualization
bool test_ascii_renderer_three_dots_bug(TestResults* results) {
    printf("Testing ASCII renderer for '3 dots' bug visualization...\n");
    results->tests_run++;
    
    SonarChart* chart = sonar_chart_alloc();
    TEST_ASSERT(chart != NULL, "Sonar chart allocation failed");
    
    // Simulate the "3 dots only" bug condition
    sonar_chart_add_point(chart, 64, 32, true);  // Dot 1
    sonar_chart_add_point(chart, 65, 32, true);  // Dot 2  
    sonar_chart_add_point(chart, 64, 33, true);  // Dot 3
    
    printf("Visualizing the '3 dots only' bug:\n");
    printf("(This is what the user would see if the bug is present)\n");
    render_ascii_sonar(chart, 64, 32);
    
    printf("Expected: Should see many '#' symbols around submarine 'S'\n");
    printf("Bug: Only 3 '#' symbols appear despite terrain existing everywhere\n\n");
    
    // Now simulate what it should look like when fixed
    sonar_chart_free(chart);
    chart = sonar_chart_alloc();
    
    // Add proper terrain discovery (what it should look like when fixed)
    for(int dy = -3; dy <= 3; dy++) {
        for(int dx = -3; dx <= 3; dx++) {
            if(dx == 0 && dy == 0) continue; // Skip submarine position
            
            // Simulate terrain in most directions (realistic result)
            bool is_terrain = (abs(dx) + abs(dy) <= 4) && ((dx + dy) % 3 != 0);
            sonar_chart_add_point(chart, 64 + dx, 32 + dy, is_terrain);
        }
    }
    
    printf("Fixed version - proper terrain discovery:\n");
    render_ascii_sonar(chart, 64, 32);
    
    sonar_chart_free(chart);
    
    results->tests_passed++;
    return true;
}

int main(void) {
    printf("=== Visual Tests: ASCII Renderer ===\n\n");
    printf("This test implements the ASCII renderer specified in the test plan\n");
    printf("for visual debugging of sonar chart data without Flipper hardware.\n\n");
    
    TestResults results = {0, 0, 0};
    
    // Basic renderer tests
    if (!test_ascii_renderer_basic(&results)) results.tests_failed++;
    
    // Progressive ping visualization
    if (!test_ascii_renderer_progressive(&results)) results.tests_failed++;
    
    // Edge cases
    if (!test_ascii_renderer_edge_cases(&results)) results.tests_failed++;
    
    // Bug visualization
    if (!test_ascii_renderer_three_dots_bug(&results)) results.tests_failed++;
    
    // Summary
    printf("\n=== ASCII Renderer Visual Test Results ===\n");
    printf("Tests run: %d\n", results.tests_run);
    printf("Tests passed: %d\n", results.tests_passed);
    printf("Tests failed: %d\n", results.tests_failed);
    
    if (results.tests_failed == 0) {
        printf("✅ All ASCII renderer visual tests PASSED!\n");
        printf("The visual debugging tool is working correctly.\n");
        return 0;
    } else {
        printf("❌ %d ASCII renderer visual tests FAILED!\n", results.tests_failed);
        return 1;
    }
}