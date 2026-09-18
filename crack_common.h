#pragma once

#include <stdint.h>

// Password alphabet [0-9a-zA-Z] = 62 symbols, fixed by the assignment.
#define CHARSET_SIZE 62
static const char CHARSET[CHARSET_SIZE] = {
  '0','1','2','3','4','5','6','7','8','9',
  'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',
  'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z'
};

// 62^exp; fits in uint64_t for exp <= 10 (the range we ever brute-force).
static inline uint64_t pow62(uint32_t exp) {
  uint64_t r = 1;
  for (uint32_t i = 0; i < exp; i++) r *= CHARSET_SIZE;
  return r;
}

// Map a linear index in [0, 62^len) to its length-`len` password (base-62).
// Writes exactly `len` chars into out; index 0 -> "00..0". Not NUL-terminated.
static inline void index_to_password(uint64_t index, uint32_t len, char *out) {
  for (int32_t i = (int32_t)len - 1; i >= 0; i--) {
    out[i] = CHARSET[index % CHARSET_SIZE];
    index /= CHARSET_SIZE;
  }
}
