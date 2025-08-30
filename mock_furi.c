#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

// Mock Flipper Zero APIs for testing on PC

uint32_t furi_get_tick(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void furi_log_print_format(int level, const char* tag, const char* format, ...) {
    const char* level_names[] = {"NONE", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
    const char* level_name = (level >= 0 && level < 6) ? level_names[level] : "UNKNOWN";
    
    printf("[%s] %s: ", level_name, tag);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
}

// Mock Canvas functions - just print debug info
void canvas_draw_dot(void* canvas, int x, int y) {
    (void)canvas;
    printf("Draw dot at (%d,%d)\n", x, y);
}

void canvas_draw_circle(void* canvas, int x, int y, int radius) {
    (void)canvas;
    printf("Draw circle at (%d,%d) radius %d\n", x, y, radius);
}

void canvas_draw_line(void* canvas, float x1, float y1, float x2, float y2) {
    (void)canvas;
    printf("Draw line from (%.1f,%.1f) to (%.1f,%.1f)\n", x1, y1, x2, y2);
}

void canvas_draw_disc(void* canvas, int x, int y, int radius) {
    (void)canvas;
    printf("Draw disc at (%d,%d) radius %d\n", x, y, radius);
}

void canvas_printf(void* canvas, int x, int y, const char* format, ...) {
    (void)canvas;
    printf("Text at (%d,%d): ", x, y);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}