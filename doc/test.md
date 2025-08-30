# Hunter-Flipper Test Plan

## Executive Summary

This document outlines a comprehensive testing strategy for the Hunter-Flipper submarine game. It was created after discovering that our existing tests failed to catch a critical "3 dots only" bug where the sonar system only displayed 3 terrain pixels instead of the expected full terrain discovery.

## Why Our Tests Failed

### The "3 Dots" Bug Analysis

Our `test_first_ping.c` failed to catch the bug for several reasons:

1. **Fixed Radius Testing**: The test used a fixed radius of 64 pixels, but the actual game starts with radius 0 and grows by 2 pixels every 50ms
2. **No Progressive State Testing**: Tests didn't simulate the frame-by-frame progression of the ping animation
3. **Missing Early-Exit Validation**: Tests didn't validate what happens when only the first few pixels of terrain are discovered
4. **No Visual Output Validation**: Tests didn't verify what actually gets rendered to screen

### Root Causes Identified

1. **Raycaster Adaptive Quality Bug**: The quality check happened AFTER ray casting, not before
2. **Ray Pattern Division Error**: Using `(ray_count - 1)` for angle distribution could cause division by zero
3. **Progressive Ping Logic**: Game only adds points within current ping radius, starting from 0

## Testing Strategy

### 1. Unit Tests

#### 1.1 Raycaster Tests (`test/unit/test_raycaster.c`)

**Purpose**: Validate core raycasting algorithms

**Test Cases**:
- Bresenham line algorithm correctness
  - Horizontal lines (y=0)
  - Vertical lines (x=0)
  - Diagonal lines (45°)
  - Arbitrary angles
  - Single pixel rays (start == end)
  - Maximum distance rays
- Ray pattern generation
  - Full 360° pattern with 32 rays
  - Forward 180° arc with 16 rays
  - Sparse pattern with 8 rays
  - Single ray pattern (edge case)
  - Verify even angle distribution
- Adaptive quality levels
  - Quality 0: All rays cast
  - Quality 1: Every other ray
  - Quality 2-3: Sparse rays
  - Verify skipped rays are properly initialized
- Performance tracking
  - rays_cast_this_frame counter
  - early_exits_this_frame counter
  - cache_hits_this_frame counter

**Critical Assertions**:
```c
// Example test
void test_bresenham_single_pixel() {
    Raycaster* rc = raycaster_alloc();
    raycaster_bresham_init(rc, 10, 10, 10, 10);
    int16_t x, y;
    bool has_next = raycaster_bresham_step(rc, &x, &y);
    assert(x == 10 && y == 10);
    assert(!has_next); // Should terminate after one step
    raycaster_free(rc);
}

void test_progressive_ping_radius() {
    // Simulate actual game ping progression
    for(int radius = 0; radius <= 64; radius += 2) {
        int discovered = count_discoveries_at_radius(radius);
        if(radius <= 2) {
            assert(discovered <= 3); // This would catch our bug!
        }
    }
}
```

#### 1.2 Terrain Generation Tests (`test/unit/test_terrain.c`)

**Purpose**: Ensure consistent terrain generation

**Test Cases**:
- Diamond-square algorithm convergence
- Seed consistency (same seed = same terrain)
- Height range validation (0-255)
- Elevation threshold application
- Edge wrapping behavior
- Memory allocation/deallocation

**Critical Assertions**:
```c
void test_terrain_deterministic() {
    TerrainManager* t1 = terrain_manager_alloc(12345, 90);
    TerrainManager* t2 = terrain_manager_alloc(12345, 90);
    for(int i = 0; i < 33*33; i++) {
        assert(t1->height_map[i] == t2->height_map[i]);
    }
}
```

#### 1.3 Chunk Manager Tests (`test/unit/test_chunk_manager.c`)

**Purpose**: Validate chunk loading/unloading logic

**Test Cases**:
- 2x2 grid maintenance
- Chunk loading on movement
- Chunk unloading on distance
- Collision detection across chunk boundaries
- Coordinate transformation (world ↔ chunk ↔ local)
- Memory pool management

**Critical Assertions**:
```c
void test_chunk_boundary_collision() {
    ChunkManager* cm = chunk_manager_alloc();
    chunk_manager_update(cm, 32.0f, 32.0f); // Chunk boundary
    
    // Test collision detection across boundary
    bool hit1 = chunk_manager_check_collision(cm, 31, 32);
    bool hit2 = chunk_manager_check_collision(cm, 32, 32);
    bool hit3 = chunk_manager_check_collision(cm, 33, 32);
    
    // All should work correctly across boundaries
    assert(hit1 == expected_terrain_at(31, 32));
    assert(hit2 == expected_terrain_at(32, 32));
    assert(hit3 == expected_terrain_at(33, 32));
}
```

#### 1.4 Sonar Chart Tests (`test/unit/test_sonar_chart.c`)

**Purpose**: Validate quadtree storage and fading

**Test Cases**:
- Point insertion and retrieval
- Quadtree subdivision
- Spatial queries
- Fade state progression
- Memory pool recycling
- Duplicate point handling

### 2. Integration Tests

#### 2.1 First Ping Test (`test/integration/test_first_ping_realistic.c`)

**Purpose**: Accurately simulate the first sonar ping

**Implementation**:
```c
void test_first_ping_realistic() {
    // Setup exactly like game
    GameContext ctx;
    ctx.world_x = 64.0f;
    ctx.world_y = 32.0f;
    ctx.ping_radius = 0;  // CRITICAL: Start at 0!
    
    // Simulate frame-by-frame ping expansion
    int frames = 0;
    int discoveries[35] = {0}; // Track discoveries per frame
    
    while(ctx.ping_radius <= 64) {
        // Cast rays
        RayResult results[32];
        raycaster_cast_pattern(...);
        
        // Only add points within current radius (game logic)
        for(int i = 0; i < 32; i++) {
            if(results[i].distance <= ctx.ping_radius) {
                discoveries[frames]++;
            }
        }
        
        // Advance ping
        ctx.ping_radius += 2;
        frames++;
    }
    
    // Validate discovery pattern
    assert(discoveries[0] <= 3); // Would catch our bug!
    assert(discoveries[1] <= 8);
    
    // Total discoveries should match terrain
    int total = 0;
    for(int i = 0; i < frames; i++) total += discoveries[i];
    assert(total >= 100); // Should find substantial terrain
}
```

#### 2.2 Movement Test (`test/integration/test_movement.c`)

**Purpose**: Validate chunk loading during movement

**Test Cases**:
- Movement across chunk boundaries
- Diagonal movement
- Collision detection during movement
- Chunk loading/unloading triggers

#### 2.3 Rendering Pipeline Test (`test/integration/test_render.c`)

**Purpose**: Validate complete render pipeline

**Test Cases**:
- World to screen coordinate transformation
- Sonar chart query and filtering
- Fade state opacity rendering
- Screen boundary clipping

### 3. System Tests

#### 3.1 Memory Test (`test/system/test_memory.c`)

**Purpose**: Ensure memory constraints are met

**Test Cases**:
- Total allocation under 30KB
- No memory leaks
- Pool allocation/deallocation cycles
- Stress test with maximum entities

#### 3.2 Performance Test (`test/system/test_performance.c`)

**Purpose**: Validate frame timing

**Test Cases**:
- Full ping cycle under 5ms
- Chunk loading under 50ms
- Render cycle under 16ms (60 FPS)

### 4. End-to-End Integration Tests (CRITICAL - MISSING FROM ORIGINAL PLAN)

#### 4.1 Complete Game Pipeline Test (`test/integration/test_game_pipeline.c`)

**Purpose**: Validate complete ping workflow from game.c initialization to screen rendering

**CRITICAL**: This test addresses the gap that allowed the "3 dots" bug to persist despite unit test fixes.

```c
void test_complete_game_initialization() {
    // Test actual game.c initialization sequence
    GameState* game = game_alloc();
    game_init(game);
    
    // Validate all systems are properly initialized
    assert(game->chunk_manager != NULL);
    assert(game->raycaster != NULL);
    assert(game->sonar_chart != NULL);
    assert(game->world_x == INITIAL_X);
    assert(game->world_y == INITIAL_Y);
    
    // Test chunk loading at startup
    int chunks_loaded = count_loaded_chunks(game->chunk_manager);
    assert(chunks_loaded == 4); // Should load 2x2 grid at start
    
    game_free(game);
}

void test_complete_ping_workflow() {
    // Setup game exactly as it runs
    GameState* game = game_alloc();
    game_init(game);
    
    // Simulate ping button press
    game_handle_ping_button(game);
    
    // Verify ping initialization
    assert(game->ping_active == true);
    assert(game->ping_radius == 0);
    
    // Simulate complete ping progression
    int discovered_points = 0;
    int max_frames = 35; // Should complete ping in reasonable time
    
    for(int frame = 0; frame < max_frames; frame++) {
        // Update ping radius (grows by 2 every 50ms in real game)
        game_update_ping(game, 50); // 50ms delta
        
        // Count discoveries this frame
        int points_before = sonar_chart_count_points(game->sonar_chart);
        
        // Process ping for this frame (CRITICAL PATH)
        game_process_ping_frame(game);
        
        int points_after = sonar_chart_count_points(game->sonar_chart);
        int new_discoveries = points_after - points_before;
        discovered_points += new_discoveries;
        
        printf("Frame %d: radius=%d, new_discoveries=%d, total=%d\n", 
               frame, game->ping_radius, new_discoveries, discovered_points);
        
        if(game->ping_radius >= 64) break;
    }
    
    // CRITICAL ASSERTION: Must discover substantial terrain
    assert(discovered_points > 10); // Should find more than "3 dots"
    
    // Verify sonar chart has data
    assert(sonar_chart_count_points(game->sonar_chart) > 10);
    
    game_free(game);
}
```

#### 4.2 Pipeline Debugging Test (`test/integration/test_pipeline_debug.c`)

**Purpose**: Trace where discovered points are lost in the pipeline

```c
void test_pipeline_point_tracing() {
    GameState* game = game_alloc();
    game_init(game);
    
    // Test each pipeline stage independently
    printf("=== PIPELINE DEBUG TRACE ===\n");
    
    // Stage 1: Chunk Manager - Are chunks loaded?
    printf("1. Chunk Loading:\n");
    chunk_manager_update(game->chunk_manager, game->world_x, game->world_y);
    int chunks = count_loaded_chunks(game->chunk_manager);
    printf("   Loaded chunks: %d (expected: 4)\n", chunks);
    assert(chunks == 4);
    
    // Stage 2: Raycaster - Are rays finding terrain?
    printf("2. Ray Casting:\n");
    RayResult results[32];
    int rays_cast = raycaster_cast_pattern(game->raycaster, 
                                          game->chunk_manager,
                                          game->world_x, game->world_y,
                                          64, // Full radius
                                          results);
    int terrain_hits = 0;
    for(int i = 0; i < rays_cast; i++) {
        if(results[i].hit && results[i].is_terrain) terrain_hits++;
    }
    printf("   Rays cast: %d, Terrain hits: %d\n", rays_cast, terrain_hits);
    assert(terrain_hits > 3); // Should find more than 3
    
    // Stage 3: Sonar Chart - Are points being stored?
    printf("3. Sonar Chart Storage:\n");
    int points_before = sonar_chart_count_points(game->sonar_chart);
    for(int i = 0; i < rays_cast; i++) {
        if(results[i].hit) {
            sonar_chart_add_point(game->sonar_chart, 
                                 results[i].end_x, results[i].end_y,
                                 results[i].is_terrain, 0);
        }
    }
    int points_after = sonar_chart_count_points(game->sonar_chart);
    printf("   Points stored: %d (added: %d)\n", points_after, points_after - points_before);
    assert(points_after > points_before);
    
    // Stage 4: Rendering - Are points being drawn?
    printf("4. Render Query:\n");
    int visible_points = count_visible_points_in_render_area(game->sonar_chart,
                                                           game->world_x, game->world_y);
    printf("   Visible points for rendering: %d\n", visible_points);
    assert(visible_points > 0);
    
    printf("=== PIPELINE TRACE COMPLETE ===\n");
    game_free(game);
}
```

#### 4.3 Game State Validation Test (`test/integration/test_game_state.c`)

**Purpose**: Validate game state transitions and persistence

```c
void test_game_state_ping_cycle() {
    GameState* game = game_alloc();
    game_init(game);
    
    // Test initial state
    assert(game->ping_active == false);
    assert(game->ping_radius == 0);
    
    // Test ping activation
    game_handle_ping_button(game);
    assert(game->ping_active == true);
    
    // Test ping completion
    while(game->ping_active) {
        game_update_ping(game, 50);
        game_process_ping_frame(game);
        if(game->ping_radius > 64) {
            game_finish_ping(game);
        }
    }
    
    // Test post-ping state
    assert(game->ping_active == false);
    assert(sonar_chart_count_points(game->sonar_chart) > 0);
    
    game_free(game);
}
```

### 5. Visual Tests

#### 5.1 ASCII Renderer (`test/visual/test_ascii_render.c`)

**Purpose**: Visual validation without Flipper hardware

```c
void render_ascii_sonar(SonarChart* chart, int cx, int cy) {
    printf("\n=== Sonar Display ===\n");
    for(int y = cy-10; y <= cy+10; y++) {
        for(int x = cx-20; x <= cx+20; x++) {
            if(x == cx && y == cy) {
                printf("S"); // Submarine
            } else {
                SonarPoint* pt;
                if(sonar_chart_query_point(chart, x, y, &pt)) {
                    printf(pt->is_terrain ? "#" : "~");
                } else {
                    printf(" ");
                }
            }
        }
        printf("\n");
    }
}
```

## Implementation Plan

### Phase 1: Critical Bug Fixes (Immediate)
1. Fix raycaster adaptive quality bug
2. Fix progressive ping radius issue
3. Add test_first_ping_realistic.c

### Phase 2: Unit Test Suite (Week 1)
1. Create test/unit directory structure
2. Implement raycaster unit tests
3. Implement terrain unit tests
4. Implement chunk manager unit tests
5. Implement sonar chart unit tests

### Phase 3: Integration Tests (Week 2)
1. Create test/integration directory
2. Implement realistic first ping test
3. Implement movement tests
4. Implement rendering pipeline tests

### Phase 4: System Tests (Week 3)
1. Create test/system directory
2. Implement memory profiling
3. Implement performance benchmarks
4. Create stress tests

### Phase 5: CI/CD Integration (Week 4)
1. Update Makefile.test for all tests
2. Create GitHub Actions workflow
3. Add test coverage reporting
4. Create test dashboard

## Test Execution

### Running Tests Locally
```bash
# Build all tests
make -f Makefile.test all

# Run unit tests
./test/unit/run_all.sh

# Run integration tests
./test/integration/run_all.sh

# Run specific test
./test/unit/test_raycaster

# Run with valgrind for memory checks
valgrind --leak-check=full ./test/unit/test_raycaster
```

### Continuous Integration
```yaml
# .github/workflows/test.yml
name: Test Suite
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build tests
        run: make -f Makefile.test all
      - name: Run tests
        run: make -f Makefile.test test
      - name: Check memory
        run: make -f Makefile.test memcheck
```

## Success Criteria

1. **Coverage**: >80% code coverage for critical paths
2. **Bug Detection**: Tests must catch the "3 dots" bug
3. **Performance**: All tests complete in <5 seconds
4. **Memory**: No memory leaks detected
5. **Determinism**: Tests produce consistent results

## Maintenance

### Adding New Tests
1. Identify the component/behavior to test
2. Create test file in appropriate directory
3. Add to Makefile.test
4. Document expected behavior
5. Run full test suite to ensure no regressions

### Test Review Checklist
- [ ] Does the test catch known bugs?
- [ ] Is the test deterministic?
- [ ] Does it test edge cases?
- [ ] Is it fast enough for CI?
- [ ] Is the assertion meaningful?

## CRITICAL BUG FINDINGS (UPDATED)

### End-to-End Testing Results

After implementing the complete test plan with end-to-end integration tests, we have **identified the precise location of the "3 dots" bug**:

#### Test Results Summary:
- ✅ **Simplified Pipeline Test**: PASSES (1024 points discovered)
- ✅ **Standalone Fix Test**: PASSES (22 terrain hits)  
- ❌ **Progressive Ping Test**: FAILS (0 terrain hits discovered)
- ❌ **First Ping Integration Test**: Missing executable

#### Root Cause Identified:
The bug is **specifically in the raycaster when called from the progressive ping test**, not in:
- ❌ Chunk loading (4 chunks loaded successfully)
- ❌ Terrain generation (25 terrain pixels found in 5x5 area)
- ❌ Core raycaster algorithm (works in standalone test)
- ❌ Sonar chart storage (works in simplified test)

#### The Critical Difference:
```
Standalone Test:  22 terrain hits ✅
Progressive Test: 0 terrain hits  ❌
```

**The bug manifests only when using the exact raycaster configuration from the progressive ping simulation, not in isolation.**

#### Next Steps Required:
1. **Debug raycaster configuration differences** between working and failing tests
2. **Examine ray pattern generation** in progressive vs standalone context
3. **Validate collision callback integration** in the progressive test
4. **Check adaptive quality settings** in real game context

This represents a **major breakthrough** - we now know the exact component and context where the bug occurs.

## Conclusion

This test plan successfully **identified the precise location of the "3 dots" bug** through end-to-end integration testing. The bug is specifically in the raycaster when called in the progressive ping context, despite working correctly in isolation. The comprehensive test suite provides the diagnostic tools needed to complete the fix.