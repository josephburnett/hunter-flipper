// Test 5.1: Quadtree Structure Validation
// This test validates quadtree internal structure after operations

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

// Test helper function to create a test chart
SonarChart* create_test_chart() {
    SonarChart* chart = malloc(sizeof(SonarChart));
    if(!chart) return NULL;
    
    // Initialize memory pools
    if(!sonar_node_pool_init(&chart->node_pool, 64)) {
        free(chart);
        return NULL;
    }
    
    if(!sonar_point_pool_init(&chart->point_pool, 128)) {
        sonar_node_pool_cleanup(&chart->node_pool);
        free(chart);
        return NULL;
    }
    
    // Create root node with test bounds
    SonarBounds root_bounds = sonar_bounds_create(-100, -100, 200, 200);
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

// Global counters for tree analysis
int total_nodes = 0;
int leaf_nodes = 0;
int total_points_in_tree = 0;
int max_depth_found = 0;
bool structure_valid = true;

// Recursive validation function
void validate_node_recursive(SonarQuadNode* node, int depth) {
    if(!node) {
        printf("ERROR: NULL node at depth %d\n", depth);
        structure_valid = false;
        return;
    }
    
    total_nodes++;
    
    if(depth > max_depth_found) {
        max_depth_found = depth;
    }
    
    printf("Depth %d: Bounds=(%d,%d)-(%d,%d), Leaf=%s, Points=%d\n",
           depth, node->bounds.min_x, node->bounds.min_y,
           node->bounds.max_x, node->bounds.max_y,
           node->is_leaf ? "true" : "false", node->point_count);
    
    if(node->is_leaf) {
        leaf_nodes++;
        total_points_in_tree += node->point_count;
        
        // Verify points are within bounds
        for(int i = 0; i < node->point_count; i++) {
            SonarPoint* p = node->points[i];
            if(!p) {
                printf("ERROR: NULL point %d in leaf at depth %d\n", i, depth);
                structure_valid = false;
                continue;
            }
            
            printf("  Point %d: (%d,%d) terrain=%d\n", i+1, p->world_x, p->world_y, p->is_terrain);
            
            // Check point is within node bounds
            if(p->world_x < node->bounds.min_x || p->world_x > node->bounds.max_x ||
               p->world_y < node->bounds.min_y || p->world_y > node->bounds.max_y) {
                printf("ERROR: Point (%d,%d) outside bounds (%d,%d)-(%d,%d)\n",
                       p->world_x, p->world_y,
                       node->bounds.min_x, node->bounds.min_y,
                       node->bounds.max_x, node->bounds.max_y);
                structure_valid = false;
            }
        }
        
        // Check if leaf has too many points
        if(node->point_count > SONAR_QUADTREE_MAX_POINTS) {
            printf("WARNING: Leaf has %d points (max=%d) - should have subdivided\n",
                   node->point_count, SONAR_QUADTREE_MAX_POINTS);
        }
        
    } else {
        // Non-leaf node - should have children
        int child_count = 0;
        for(int i = 0; i < 4; i++) {
            if(node->children[i]) {
                child_count++;
                
                // Validate child bounds are properly subdivided
                SonarQuadNode* child = node->children[i];
                
                // Check child bounds fit within parent
                if(child->bounds.min_x < node->bounds.min_x || 
                   child->bounds.max_x > node->bounds.max_x ||
                   child->bounds.min_y < node->bounds.min_y ||
                   child->bounds.max_y > node->bounds.max_y) {
                    printf("ERROR: Child %d bounds exceed parent bounds\n", i);
                    structure_valid = false;
                }
                
                validate_node_recursive(child, depth + 1);
            }
        }
        
        if(child_count == 0) {
            printf("ERROR: Non-leaf node has no children\n");
            structure_valid = false;
        }
        
        // Non-leaf should have point_count = 0 (points moved to children)
        if(node->point_count > 0) {
            printf("WARNING: Non-leaf node has %d points - should be 0\n", node->point_count);
        }
    }
}

// Test function: Validate quadtree structure
bool test_validate_quadtree() {
    printf("=== Test 5.1: Quadtree Structure Validation ===\n");
    
    SonarChart* chart = create_test_chart();
    if(!chart) {
        printf("FAIL: Could not create test chart\n");
        return false;
    }
    
    printf("Step 1: Adding points that should trigger subdivision...\n");
    
    // Reset global counters
    total_nodes = 0;
    leaf_nodes = 0;
    total_points_in_tree = 0;
    max_depth_found = 0;
    structure_valid = true;
    
    // Add points that will trigger subdivision
    int points_added = 0;
    for(int i = 0; i < SONAR_QUADTREE_MAX_POINTS + 10; i++) {
        int x = 60 + (i % 6);  // Spread across small area to force subdivision
        int y = 50 + (i / 6);
        
        bool added = sonar_chart_add_point(chart, x, y, true);
        if(added) {
            points_added++;
            if(i < 5 || i == SONAR_QUADTREE_MAX_POINTS || i >= SONAR_QUADTREE_MAX_POINTS + 8) {
                printf("  Added point %d at (%d,%d)\n", i+1, x, y);
            }
        } else {
            printf("  FAILED to add point %d at (%d,%d)\n", i+1, x, y);
        }
    }
    
    printf("Successfully added %d points\n", points_added);
    
    printf("\nStep 2: Validating tree structure...\n");
    
    // Recursive validation function
    validate_node_recursive(chart->root, 0);
    
    printf("\nStep 3: Structure analysis summary...\n");
    printf("Total nodes in tree: %d\n", total_nodes);
    printf("Leaf nodes: %d\n", leaf_nodes);
    printf("Internal nodes: %d\n", total_nodes - leaf_nodes);
    printf("Maximum depth: %d\n", max_depth_found);
    printf("Points found in tree structure: %d\n", total_points_in_tree);
    printf("Points originally added: %d\n", points_added);
    printf("Structure validation: %s\n", structure_valid ? "PASSED" : "FAILED");
    
    if(total_points_in_tree < points_added) {
        printf("❌ CRITICAL BUG: Tree structure is missing %d points!\n", 
               points_added - total_points_in_tree);
        printf("This confirms points are lost during subdivision operations.\n");
        structure_valid = false;
    }
    
    printf("\nStep 4: Query validation...\n");
    
    // Compare tree traversal count with query results
    SonarBounds query = {-200, -200, 400, 400};
    SonarPoint* points[100];
    uint16_t query_count = sonar_chart_query_area(chart, query, points, 100);
    
    printf("Query returned: %d points\n", query_count);
    printf("Tree structure contains: %d points\n", total_points_in_tree);
    
    if(query_count != total_points_in_tree) {
        printf("❌ QUERY BUG: Query returns different count than tree structure!\n");
        printf("This suggests the query algorithm has bugs.\n");
        structure_valid = false;
    }
    
    printf("\nStep 5: Memory pool analysis...\n");
    printf("Point pool active count: %d\n", chart->point_pool.active_count);
    printf("Expected active count: %d\n", points_added);
    
    if(chart->point_pool.active_count != points_added) {
        printf("❌ MEMORY BUG: Point pool count mismatch!\n");
        printf("Pool shows %d active, but %d were added successfully\n",
               chart->point_pool.active_count, points_added);
    }
    
    // Cleanup
    sonar_chart_free(chart);
    
    if(structure_valid && total_points_in_tree == points_added && query_count == points_added) {
        printf("\n✓ Test 5.1 PASSED: Quadtree structure is valid\n");
        return true;
    } else {
        printf("\n❌ Test 5.1 FAILED: Quadtree structure has critical bugs\n");
        return false;
    }
}

// Test function: Boundary subdivision analysis
bool test_boundary_subdivision_analysis() {
    printf("\n=== Test 5.1b: Boundary Subdivision Analysis ===\n");
    
    SonarChart* chart = create_test_chart();
    if(!chart) {
        printf("FAIL: Could not create test chart\n");
        return false;
    }
    
    printf("Analyzing subdivision behavior at boundaries...\n");
    
    // Calculate the midpoint of the root bounds for boundary testing
    int mid_x = (chart->root->bounds.min_x + chart->root->bounds.max_x) / 2;
    int mid_y = (chart->root->bounds.min_y + chart->root->bounds.max_y) / 2;
    
    printf("Root bounds: (%d,%d) to (%d,%d)\n",
           chart->root->bounds.min_x, chart->root->bounds.min_y,
           chart->root->bounds.max_x, chart->root->bounds.max_y);
    printf("Calculated midpoint: (%d,%d)\n", mid_x, mid_y);
    
    // Add points exactly at subdivision boundaries
    struct { int x, y; const char* desc; } boundary_points[] = {
        {mid_x, mid_y, "center"},
        {mid_x - 1, mid_y, "left of center"},
        {mid_x + 1, mid_y, "right of center"},
        {mid_x, mid_y - 1, "above center"},
        {mid_x, mid_y + 1, "below center"},
        {mid_x - 1, mid_y - 1, "top-left quadrant"},
        {mid_x + 1, mid_y - 1, "top-right quadrant"},
        {mid_x - 1, mid_y + 1, "bottom-left quadrant"},
        {mid_x + 1, mid_y + 1, "bottom-right quadrant"}
    };
    
    int num_boundary_points = sizeof(boundary_points) / sizeof(boundary_points[0]);
    int boundary_added = 0;
    
    printf("\nAdding boundary points:\n");
    for(int i = 0; i < num_boundary_points; i++) {
        bool added = sonar_chart_add_point(chart, boundary_points[i].x, boundary_points[i].y, true);
        if(added) {
            boundary_added++;
            printf("  ✓ Added (%d,%d) - %s\n", boundary_points[i].x, boundary_points[i].y, boundary_points[i].desc);
        } else {
            printf("  ❌ Failed (%d,%d) - %s\n", boundary_points[i].x, boundary_points[i].y, boundary_points[i].desc);
        }
    }
    
    // Add more points to force subdivision
    printf("\nAdding additional points to force subdivision...\n");
    int additional_added = 0;
    for(int i = 0; i < SONAR_QUADTREE_MAX_POINTS; i++) {
        int x = mid_x + (i % 5) - 2;  // Spread around midpoint
        int y = mid_y + (i / 5) - 2;
        
        bool added = sonar_chart_add_point(chart, x, y, true);
        if(added) additional_added++;
    }
    
    printf("Added %d additional points\n", additional_added);
    
    int total_expected = boundary_added + additional_added;
    
    // Analyze the resulting structure
    printf("\nAnalyzing resulting structure:\n");
    total_nodes = 0;
    leaf_nodes = 0;
    total_points_in_tree = 0;
    max_depth_found = 0;
    structure_valid = true;
    
    validate_node_recursive(chart->root, 0);
    
    printf("\nBoundary test results:\n");
    printf("Expected points: %d\n", total_expected);
    printf("Points in structure: %d\n", total_points_in_tree);
    printf("Points lost: %d\n", total_expected - total_points_in_tree);
    
    // Test boundary point retrieval
    printf("\nTesting boundary point retrieval:\n");
    for(int i = 0; i < num_boundary_points; i++) {
        SonarBounds exact = {boundary_points[i].x, boundary_points[i].y, 
                           boundary_points[i].x, boundary_points[i].y};
        SonarPoint* points[5];
        uint16_t count = sonar_chart_query_area(chart, exact, points, 5);
        
        printf("  Query (%d,%d) %s: %d points found\n", 
               boundary_points[i].x, boundary_points[i].y, 
               boundary_points[i].desc, count);
    }
    
    // Cleanup
    sonar_chart_free(chart);
    
    if(total_points_in_tree == total_expected) {
        printf("\n✓ Boundary subdivision test passed\n");
        return true;
    } else {
        printf("\n❌ Boundary subdivision test failed\n");
        return false;
    }
}

int main() {
    printf("Hunter-Flipper Test Suite: Phase 5 - Debug Analysis\n");
    printf("Test File: test_quadtree_structure.c\n");
    printf("Purpose: Validate quadtree internal structure and identify bugs\n\n");
    
    bool all_passed = true;
    
    all_passed &= test_validate_quadtree();
    all_passed &= test_boundary_subdivision_analysis();
    
    if(all_passed) {
        printf("🎉 ALL STRUCTURE TESTS PASSED\n");
        printf("The quadtree internal structure appears valid.\n");
        printf("If bugs exist, they may be in edge cases or specific conditions.\n");
        return 0;
    } else {
        printf("❌ STRUCTURE TESTS FAILED\n");
        printf("CRITICAL BUGS FOUND in quadtree internal structure!\n");
        printf("This confirms the root cause of the 'single pixel land' bug.\n");
        return 1;
    }
}