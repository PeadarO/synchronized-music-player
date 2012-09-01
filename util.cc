#include "util.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include <sys/time.h>

void Die(const char* message, ...) {
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  fprintf(stderr, "\n");
  exit(-1);
}

void Warn(const char* message, ...) {
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  fprintf(stderr, "\n");
}

int64_t UnixUsNow() {
  timeval now;
  if (gettimeofday(&now, NULL /* tz */) != 0) {
    Die("gettimeofday failed");
  }
  return now.tv_sec * 1000000LL + now.tv_usec;
}

