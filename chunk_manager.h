#pragma once
#include "engine/engine.h"
#include "terrain.h"

// Chunk system configuration - optimized for Flipper Zero memory  
#define CHUNK_SIZE 33          // Size of each chunk (33x33, reduced from 65)
#define CHUNK_GRID_SIZE 2      // 2x2 grid of active chunks (reduced from 3x3)
#define MAX_ACTIVE_CHUNKS 4    // Total active chunks at once (reduced from 9)
#define CHUNK_CACHE_SIZE 0     // No LRU cache to save memory
#define CHUNK_LOAD_DISTANCE 24 // Distance from chunk edge to trigger loading

// Chunk coordinates
typedef struct {
    int chunk_x;
    int chunk_y;
} ChunkCoord;

// Individual chunk data
typedef struct {
    ChunkCoord coord;
    TerrainManager* terrain;
    bool is_loaded;
    bool is_dirty;
    uint32_t last_access_time;
    uint32_t generation_seed;
} TerrainChunk;

// Memory pool for chunk allocation
typedef struct {
    TerrainChunk* chunks;
    bool* chunk_in_use;
    uint32_t pool_size;
    uint32_t next_free;
} ChunkPool;

// LRU cache for recently unloaded chunks (disabled for memory savings)
typedef struct {
    uint32_t dummy; // Placeholder to avoid empty struct
} ChunkCache;

// Main chunk manager
typedef struct {
    // Active chunk grid (3x3 around player)
    TerrainChunk* active_chunks[MAX_ACTIVE_CHUNKS];
    ChunkCoord center_chunk;
    
    // Memory management
    ChunkPool pool;
    ChunkCache cache;
    
    // Player position tracking
    float player_world_x;
    float player_world_y;
    
    // Performance monitoring
    uint32_t chunks_loaded_this_frame;
    uint32_t chunks_unloaded_this_frame;
    uint32_t generation_time_ms;
} ChunkManager;

// Chunk manager lifecycle
ChunkManager* chunk_manager_alloc(void);
void chunk_manager_free(ChunkManager* manager);

// Core chunk operations
void chunk_manager_update(ChunkManager* manager, float player_x, float player_y);
TerrainChunk* chunk_manager_get_chunk_at(ChunkManager* manager, int world_x, int world_y);
bool chunk_manager_check_collision(ChunkManager* manager, int world_x, int world_y);

// Chunk loading/unloading
TerrainChunk* chunk_manager_load_chunk(ChunkManager* manager, ChunkCoord coord);
void chunk_manager_unload_chunk(ChunkManager* manager, TerrainChunk* chunk);
TerrainChunk* chunk_manager_get_from_cache(ChunkManager* manager, ChunkCoord coord);
void chunk_manager_cache_chunk(ChunkManager* manager, TerrainChunk* chunk);

// Utility functions
ChunkCoord world_to_chunk_coord(float world_x, float world_y);
bool chunk_coord_equals(ChunkCoord a, ChunkCoord b);
uint32_t chunk_coord_hash(ChunkCoord coord);
int chunk_manager_get_active_index(ChunkManager* manager, ChunkCoord coord);

// Memory pool management
bool chunk_pool_init(ChunkPool* pool, uint32_t size);
void chunk_pool_cleanup(ChunkPool* pool);
TerrainChunk* chunk_pool_alloc(ChunkPool* pool);
void chunk_pool_free(ChunkPool* pool, TerrainChunk* chunk);

// Performance monitoring
void chunk_manager_reset_frame_stats(ChunkManager* manager);
void chunk_manager_log_performance(ChunkManager* manager);