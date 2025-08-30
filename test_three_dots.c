#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

// Simple terrain check at submarine position
int main(void) {
    printf("=== Three Dots Investigation ===\n\n");
    
    // Submarine world position
    float world_x = 64.0f;
    float world_y = 32.0f;
    
    // Calculate chunk and local position
    int chunk_x = (int)floorf(world_x / 32.0f);  // CHUNK_SIZE - 1 = 32
    int chunk_y = (int)floorf(world_y / 32.0f);
    int local_x = (int)world_x - (chunk_x * 32);
    int local_y = (int)world_y - (chunk_y * 32);
    
    printf("World position: (%.1f, %.1f)\n", world_x, world_y);
    printf("Chunk coordinate: (%d, %d)\n", chunk_x, chunk_y);
    printf("Local position in chunk: (%d, %d)\n\n", local_x, local_y);
    
    // From my test output, terrain at (0,0) in chunk looks like:
    // Row 0: #################### (all land)
    // Row 1: #################### (all land)  
    // Row 2: #################### (all land)
    // Row 3: ###################. (mostly land)
    
    printf("Simulating raycasting from position (0,0) in chunk:\n");
    printf("Based on terrain pattern, rays going:\n");
    printf("  North (0,-1): Would hit land immediately\n");
    printf("  South (0,+1): Would hit land immediately\n");
    printf("  East (+1,0): Would hit land immediately\n");
    printf("  West (-1,0): Would hit land immediately\n");
    printf("  NE, NW, SE, SW: All would hit land immediately\n\n");
    
    printf("Expected: With 32 rays in all directions, should hit land in most directions\n");
    printf("Reality: Only 3 dots appear\n\n");
    
    printf("HYPOTHESIS 1: Raycasting is broken\n");
    printf("  - Rays might not be stepping correctly\n");
    printf("  - Bresenham algorithm might have a bug\n");
    printf("  - Ray directions might be wrong\n\n");
    
    printf("HYPOTHESIS 2: Coordinate conversion is broken\n");
    printf("  - chunk_manager_check_collision might have wrong math\n");
    printf("  - Local coordinates might be calculated wrong\n");
    printf("  - Chunk lookup might fail\n\n");
    
    printf("HYPOTHESIS 3: Only 3 rays are actually cast\n");
    printf("  - Ray pattern might be wrong\n");
    printf("  - Loop might terminate early\n");
    printf("  - Adaptive quality might be too aggressive\n\n");
    
    // Let's check the ray pattern details
    printf("Checking ray pattern:\n");
    printf("  sonar_pattern_full: 32 rays, 360 degrees\n");
    printf("  sonar_pattern_forward: 16 rays, 180 degrees\n");
    printf("  sonar_pattern_sparse: 8 rays, 360 degrees\n");
    printf("  Adaptive pattern (quality 0): Should use full (32 rays)\n\n");
    
    // Check ray angles for 32 rays
    printf("32 rays at angles:\n");
    for(int i = 0; i < 32; i++) {
        float angle = (float)i * 2.0f * 3.14159f / 32.0f;
        float dx = cosf(angle);
        float dy = sinf(angle);
        printf("  Ray %2d: angle %.2f rad, direction (%.2f, %.2f)\n", 
               i, angle, dx, dy);
        
        // Check what this ray would hit at distance 1
        int check_x = 0 + (int)roundf(dx);
        int check_y = 0 + (int)roundf(dy);
        
        // Based on terrain pattern, positions around (0,0) are all land
        if(abs(check_x) <= 1 && abs(check_y) <= 1) {
            printf("         -> Would hit land at (%d,%d)\n", check_x, check_y);
        }
    }
    
    printf("\nCONCLUSION: With terrain all around (0,0), 32 rays should find lots of land.\n");
    printf("If only 3 dots appear, something is very wrong with raycasting or collision detection.\n");
    
    return 0;
}