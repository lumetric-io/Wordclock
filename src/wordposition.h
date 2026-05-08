#pragma once
#include <stdint.h>

struct WordPosition {
  const char* word;
  uint8_t count;
  int indices[32];
};

// Use this macro for all word definitions so count is computed at compile time.
#define WPOS(name, ...) { name, (uint8_t)(sizeof((int[]){__VA_ARGS__})/sizeof(int)), {__VA_ARGS__} }
