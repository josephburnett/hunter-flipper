#pragma once
#include "engine/engine.h"
#include "terrain.h"

// Fading sonar system constants - optimized for memory and performance
#define SONAR_FADE_DURATION_MS 30000  // 30 seconds fade time (reduced)
#define SONAR_HASH_SIZE 256           // Hash table size (reduced for memory)
#define SONAR_MAX_PER_BUCKET 4        // Max points per hash bucket (reduced)

typedef struct {
    int16_t x, y;                // World coordinates (16-bit to save memory)
    uint32_t discovered_time;    // When this point was discovered
} SonarPoint;

typedef struct {
    SonarPoint points[SONAR_MAX_PER_BUCKET];  // Fixed array per bucket
    uint8_t count;                            // Number of points in bucket
} SonarBucket;

typedef struct {
    SonarBucket* buckets;        // Hash table of buckets
    uint32_t last_cleanup_time;  // When we last cleaned up old points
} SonarChart;

typedef enum {
    GAME_MODE_NAV,
    GAME_MODE_TORPEDO
} GameMode;

typedef struct {
    // Submarine state (world coordinates)
    float world_x;
    float world_y;
    float velocity;
    float heading;
    GameMode mode;
    
    // Screen position (always centered)
    float screen_x;
    float screen_y;
    
    // Torpedo management
    uint8_t torpedo_count;
    uint8_t max_torpedoes;
    
    // Sonar state
    bool ping_active;
    float ping_x;
    float ping_y;
    uint8_t ping_radius;
    uint32_t ping_timer;
    
    // Input state
    uint32_t back_press_start;
    bool back_long_press;
    
    // Game settings
    float max_velocity;
    float turn_rate;
    float acceleration;
    
    // Terrain system
    TerrainManager* terrain;
    
    // Fading sonar chart for discovered areas
    SonarChart* sonar_chart;
} GameContext;

// Fading sonar chart functions - optimized
SonarChart* sonar_chart_alloc(void);
void sonar_chart_free(SonarChart* chart);
void sonar_chart_add_point(SonarChart* chart, int x, int y, uint32_t current_time);
bool sonar_chart_is_discovered(SonarChart* chart, int x, int y, uint32_t current_time);
void sonar_chart_cleanup_old_points(SonarChart* chart, uint32_t current_time);
float sonar_chart_get_fade_level(SonarChart* chart, int x, int y, uint32_t current_time);