#pragma once
#include "../Arduino.h"
inline int mbedtls_sha256(const unsigned char* in, size_t n, unsigned char out[32], int) {
  for (int i = 0; i < 32; i++) out[i] = (unsigned char)(i < (int)n ? in[i] : 0);
  return 0;
}
