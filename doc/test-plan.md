# Hunter Flipper Test Plan: Single Pixel Land Bug

## Problem Statement
The sonar chart only displays 1-3 pixels of terrain despite:
- Terrain being successfully generated (submarine collides with land)
- Multiple terrain points being discovered by raycasting
- Points being added to the sonar chart storage
- Log showing "32 total (1 terrain)" in render queries despite many terrain points being added

## Root Cause Analysis
Based on log analysis at `/home/joseph/hunter-flipper/log.txt`:
1. **Terrain Discovery Works**: Rays hit terrain at multiple locations (e.g., (66,51), (66,52), (70,57), (48,55))
2. **Point Storage Appears to Work**: Chart reports "Created NEW terrain point" for each discovery
3. **Query/Retrieval FAILS**: Render query returns only 1 terrain point despite many being stored
4. **Rendering Limited**: Only point (66,51) is ever drawn, always at the same screen position

## Critical Pipeline Stages to Test

### Stage 1: Terrain Generation & Storage
- **Component**: `TerrainManager`, `ChunkManager`
- **Key Functions**: `terrain_check_collision()`, `chunk_manager_check_collision()`
- **Verification**: Terrain exists and collision detection works

### Stage 2: Raycasting & Discovery
- **Component**: `Raycaster`
- **Key Functions**: `raycaster_cast_ray()`, `raycaster_bresham_step()`
- **Verification**: Rays correctly find terrain boundaries

### Stage 3: Sonar Chart Storage (SUSPECTED BUG LOCATION)
- **Component**: `SonarChart`, Quadtree implementation
- **Key Functions**: `sonar_chart_add_point()`, `sonar_quad_insert()`, `sonar_quad_subdivide()`
- **Verification**: Points are stored and retrievable

### Stage 4: Sonar Chart Querying (SUSPECTED BUG LOCATION)
- **Component**: `SonarChart` quadtree query
- **Key Functions**: `sonar_chart_query_area()`, `sonar_quad_query()`
- **Verification**: All stored points are returned by queries

### Stage 5: Rendering Pipeline
- **Component**: Game rendering loop
- **Key Functions**: `world_to_screen()`, sonar point rendering
- **Verification**: Retrieved points are transformed and drawn correctly

## Test Suite Requirements

### 1. Unit Tests - Component Isolation

#### Test 1.1: Quadtree Point Storage and Retrieval
**File**: `test/unit/test_quadtree_storage.c`
```c
// Test that multiple points added to quadtree can all be retrieved
void test_quadtree_multiple_points() {
    SonarChart* chart = create_test_chart();
    
    // Add 10 terrain points in a cluster
    for(int i = 0; i < 10; i++) {
        sonar_chart_add_point(chart, 60 + i, 50, true);
    }
    
    // Query the area
    SonarBounds bounds = {50, 40, 80, 60};
    SonarPoint* points[20];
    uint16_t count = sonar_chart_query_area(chart, bounds, points, 20);
    
    // VERIFY: All 10 points are returned
    assert(count == 10);
    
    // VERIFY: Each point has correct coordinates
    for(int i = 0; i < count; i++) {
        assert(points[i]->is_terrain == true);
        assert(points[i]->world_x >= 60 && points[i]->world_x < 70);
    }
}
```

#### Test 1.2: Quadtree Subdivision Behavior
**File**: `test/unit/test_quadtree_subdivision.c`
```c
// Test that subdivision doesn't lose points
void test_quadtree_subdivision_preserves_points() {
    SonarChart* chart = create_test_chart();
    
    // Add enough points to trigger subdivision (>SONAR_QUADTREE_MAX_POINTS)
    for(int i = 0; i < SONAR_QUADTREE_MAX_POINTS + 5; i++) {
        sonar_chart_add_point(chart, 60 + (i % 3), 50 + (i % 3), true);
    }
    
    // Query and verify all points exist
    SonarBounds bounds = {0, 0, 200, 200};
    SonarPoint* points[50];
    uint16_t count = sonar_chart_query_area(chart, bounds, points, 50);
    
    // CRITICAL: Verify no points were lost during subdivision
    assert(count == SONAR_QUADTREE_MAX_POINTS + 5);
}
```

#### Test 1.3: Duplicate Point Handling
**File**: `test/unit/test_quadtree_duplicates.c`
```c
// Test how quadtree handles duplicate points at same location
void test_quadtree_duplicate_points() {
    SonarChart* chart = create_test_chart();
    
    // Add same point multiple times
    for(int i = 0; i < 5; i++) {
        sonar_chart_add_point(chart, 66, 51, true);
    }
    
    // Query exact location
    SonarBounds bounds = {66, 51, 66, 51};
    SonarPoint* points[10];
    uint16_t count = sonar_chart_query_area(chart, bounds, points, 10);
    
    // Verify behavior - should either merge or store all
    printf("Duplicate handling: %d points at (66,51)\n", count);
    assert(count >= 1); // At least one point should exist
}
```

#### Test 1.4: Boundary Conditions in Quadtree
**File**: `test/unit/test_quadtree_boundaries.c`
```c
// Test points at quadrant boundaries during subdivision
void test_quadtree_boundary_points() {
    SonarChart* chart = create_test_chart();
    
    // Add points at boundaries that might cause subdivision issues
    int mid_x = chart->root->bounds.min_x + 
                (chart->root->bounds.max_x - chart->root->bounds.min_x) / 2;
    int mid_y = chart->root->bounds.min_y + 
                (chart->root->bounds.max_y - chart->root->bounds.min_y) / 2;
    
    // Points exactly at subdivision boundaries
    sonar_chart_add_point(chart, mid_x, mid_y, true);
    sonar_chart_add_point(chart, mid_x - 1, mid_y, true);
    sonar_chart_add_point(chart, mid_x + 1, mid_y, true);
    sonar_chart_add_point(chart, mid_x, mid_y - 1, true);
    sonar_chart_add_point(chart, mid_x, mid_y + 1, true);
    
    // Force subdivision by adding more points
    for(int i = 0; i < SONAR_QUADTREE_MAX_POINTS; i++) {
        sonar_chart_add_point(chart, mid_x + (i % 5), mid_y + (i % 5), true);
    }
    
    // Query and verify all boundary points still exist
    SonarBounds bounds = {mid_x - 2, mid_y - 2, mid_x + 2, mid_y + 2};
    SonarPoint* points[30];
    uint16_t count = sonar_chart_query_area(chart, bounds, points, 30);
    
    // Should have at least the 5 boundary points
    assert(count >= 5);
}
```

### 2. Data Flow Tracking Tests

#### Test 2.1: Point Lifecycle Tracking
**File**: `test/integration/test_point_lifecycle.c`
```c
// Track a single point through the entire pipeline
void test_single_point_lifecycle() {
    GameContext ctx = setup_game_context();
    
    // Add a specific terrain point with tracking
    int test_x = 70, test_y = 55;
    printf("TRACK: Adding point at (%d,%d)\n", test_x, test_y);
    
    // Stage 1: Add point
    bool added = sonar_chart_add_point(ctx.sonar_chart, test_x, test_y, true);
    assert(added == true);
    printf("TRACK: Point added successfully\n");
    
    // Stage 2: Query for exact point
    SonarBounds exact = {test_x, test_y, test_x, test_y};
    SonarPoint* points[5];
    uint16_t count = sonar_chart_query_area(ctx.sonar_chart, exact, points, 5);
    printf("TRACK: Exact query found %d points\n", count);
    assert(count >= 1);
    
    // Stage 3: Query wider area
    SonarBounds wide = {test_x - 10, test_y - 10, test_x + 10, test_y + 10};
    count = sonar_chart_query_area(ctx.sonar_chart, wide, points, 5);
    printf("TRACK: Wide query found %d points\n", count);
    assert(count >= 1);
    
    // Stage 4: Verify point properties
    bool found = false;
    for(int i = 0; i < count; i++) {
        if(points[i]->world_x == test_x && points[i]->world_y == test_y) {
            found = true;
            assert(points[i]->is_terrain == true);
            printf("TRACK: Point verified at index %d\n", i);
        }
    }
    assert(found == true);
}
```

#### Test 2.2: Progressive Ping Simulation
**File**: `test/integration/test_ping_progression.c`
```c
// Simulate a complete ping cycle and track discoveries
void test_ping_progression() {
    GameContext ctx = setup_game_context();
    ctx.ping_active = true;
    ctx.ping_x = 60;
    ctx.ping_y = 51;
    ctx.ping_radius = 2;
    
    int total_discoveries = 0;
    int frames = 0;
    
    while(ctx.ping_radius <= 64 && frames < 35) {
        // Simulate ping expansion
        ctx.ping_radius += 2;
        
        // Cast rays and discover terrain
        int discoveries_this_frame = simulate_ping_frame(&ctx);
        total_discoveries += discoveries_this_frame;
        
        // Query sonar chart
        SonarBounds query = {-100, -100, 200, 200};
        SonarPoint* points[500];
        uint16_t stored_count = sonar_chart_query_area(
            ctx.sonar_chart, query, points, 500
        );
        
        // Log discrepancy
        printf("Frame %d: Radius=%d, Discovered=%d, Stored=%d, Total=%d\n",
               frames, ctx.ping_radius, discoveries_this_frame, 
               stored_count, total_discoveries);
        
        // CRITICAL: Stored count should match total discoveries
        if(stored_count < total_discoveries) {
            printf("ERROR: Lost %d points!\n", total_discoveries - stored_count);
        }
        
        frames++;
    }
    
    assert(total_discoveries > 10); // Should discover many points
}
```

### 3. Reproduction Tests

#### Test 3.1: Exact Bug Reproduction
**File**: `test/integration/test_exact_bug_repro.c`
```c
// Reproduce the exact scenario from the log
void test_exact_bug_scenario() {
    GameContext ctx = setup_game_context();
    
    // Position from log
    ctx.world_x = 60;
    ctx.world_y = 51;
    
    // Add points exactly as shown in log
    sonar_chart_add_point(ctx.sonar_chart, 66, 51, true);
    sonar_chart_add_point(ctx.sonar_chart, 66, 52, true);
    sonar_chart_add_point(ctx.sonar_chart, 66, 53, true);
    sonar_chart_add_point(ctx.sonar_chart, 70, 57, true);
    sonar_chart_add_point(ctx.sonar_chart, 48, 55, true);
    // ... add all points from log
    
    // Query as render does
    SonarBounds render_query = {-20, -29, 140, 131};
    SonarPoint* points[100];
    uint16_t count = sonar_chart_query_area(
        ctx.sonar_chart, render_query, points, 100
    );
    
    // Count terrain vs water
    int terrain_count = 0;
    for(int i = 0; i < count; i++) {
        if(points[i]->is_terrain) terrain_count++;
    }
    
    printf("Query returned %d total, %d terrain\n", count, terrain_count);
    
    // BUG: Log shows "32 total (1 terrain)" but we added 5+ terrain points
    assert(terrain_count >= 5);
}
```

### 4. Stress Tests

#### Test 4.1: Large Scale Point Storage
**File**: `test/stress/test_many_points.c`
```c
// Test with realistic number of discovered points
void test_large_scale_storage() {
    SonarChart* chart = create_test_chart();
    
    // Simulate discovering entire visible area
    int points_added = 0;
    for(int x = 0; x < 128; x += 2) {
        for(int y = 0; y < 128; y += 2) {
            // Checkerboard pattern of terrain
            if((x + y) % 4 == 0) {
                sonar_chart_add_point(chart, x, y, true);
                points_added++;
            }
        }
    }
    
    printf("Added %d terrain points\n", points_added);
    
    // Query entire area
    SonarBounds all = {0, 0, 128, 128};
    SonarPoint* points[2000];
    uint16_t count = sonar_chart_query_area(chart, all, points, 2000);
    
    printf("Query returned %d points\n", count);
    assert(count == points_added);
}
```

### 5. Debugging Tests

#### Test 5.1: Quadtree Structure Validation
**File**: `test/debug/test_quadtree_structure.c`
```c
// Validate quadtree structure after operations
void test_validate_quadtree() {
    SonarChart* chart = create_test_chart();
    
    // Add points that should trigger subdivision
    for(int i = 0; i < 20; i++) {
        sonar_chart_add_point(chart, 60 + i, 50 + i, true);
    }
    
    // Recursive validation function
    validate_node_recursive(chart->root, 0);
}

void validate_node_recursive(SonarQuadNode* node, int depth) {
    printf("Depth %d: Bounds=(%d,%d)-(%d,%d), Leaf=%d, Points=%d\n",
           depth, node->bounds.min_x, node->bounds.min_y,
           node->bounds.max_x, node->bounds.max_y,
           node->is_leaf, node->point_count);
    
    if(node->is_leaf) {
        // Verify points are within bounds
        for(int i = 0; i < node->point_count; i++) {
            SonarPoint* p = node->points[i];
            assert(p->world_x >= node->bounds.min_x);
            assert(p->world_x <= node->bounds.max_x);
            assert(p->world_y >= node->bounds.min_y);
            assert(p->world_y <= node->bounds.max_y);
        }
    } else {
        // Verify children exist and recurse
        for(int i = 0; i < 4; i++) {
            assert(node->children[i] != NULL);
            validate_node_recursive(node->children[i], depth + 1);
        }
    }
}
```

## Test Execution Order

1. **Phase 1: Unit Tests** - Isolate quadtree behavior
   - Run tests 1.1-1.4 to verify basic quadtree operations
   - Focus on subdivision and boundary conditions

2. **Phase 2: Data Flow** - Track points through pipeline
   - Run tests 2.1-2.2 to identify where points are lost
   - Compare discovery count vs storage count vs query count

3. **Phase 3: Reproduction** - Exact bug scenario
   - Run test 3.1 with exact coordinates from log
   - Verify discrepancy between added and queried points

4. **Phase 4: Stress Testing** - Scale testing
   - Run test 4.1 to check if issue is related to data volume

5. **Phase 5: Debug Analysis** - Structure validation
   - Run test 5.1 to inspect quadtree internal state

## Expected Outcomes

### If Quadtree Storage is Broken:
- Test 1.1 will fail - points not retrievable after adding
- Test 1.2 will fail - points lost during subdivision
- Test 5.1 will show malformed tree structure

### If Quadtree Query is Broken:
- Test 1.1 storage works but query returns wrong count
- Test 2.1 Stage 2/3 will show discrepancy
- Test 3.1 will reproduce exact bug

### If Coordinate System is Broken:
- Points will be stored at wrong locations
- Test 1.3 will show unexpected behavior
- Test 2.1 will fail at Stage 3

### If Rendering Transform is Broken:
- All storage/query tests pass
- Visual tests show wrong screen positions
- Multiple points render at same pixel

## Implementation Notes

1. Each test should use extensive logging to track data flow
2. Use consistent coordinate system (world coordinates)
3. Verify memory pool allocation doesn't fail silently
4. Check for off-by-one errors in bounds checking
5. Validate that `sonar_bounds_contains_point()` is inclusive
6. Ensure `sonar_quad_subdivide()` correctly redistributes points

## Priority Recommendations

**HIGHEST PRIORITY**: Tests 1.2 and 3.1
- These directly test the subdivision behavior and exact bug scenario
- Most likely to reveal the root cause quickly

**HIGH PRIORITY**: Tests 1.1 and 2.2
- Basic functionality and full ping simulation
- Will show if problem is storage vs retrieval

**MEDIUM PRIORITY**: Tests 1.3, 1.4, 4.1
- Edge cases and stress testing
- Important for robustness but may not reveal primary bug

## Success Criteria

The test suite successfully identifies the bug when:
1. A specific test consistently fails
2. The failure point matches the symptoms (many points added, few returned)
3. The fix makes all tests pass
4. The sonar chart displays full terrain coverage after fix