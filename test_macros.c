// Provide MAX/MIN implementations for terrain.c
#include "test_common.h"

int MAX(int a, int b) { return (a > b) ? a : b; }
int MIN(int a, int b) { return (a < b) ? a : b; }