#pragma once

#include <numeric>
#include <stdint.h>

const int64_t kMillion = 1000000;

void Die(const char* message, ...);
void Warn(const char* message, ...);
int64_t UnixUsNow();

