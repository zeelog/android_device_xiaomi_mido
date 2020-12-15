#include <log/log.h>

int __android_log_print(int prio, const char* tag, const char* fmt, ...) {
  // Goodix: please don't spam that much!
  return 0;
}
