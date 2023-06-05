#include "util.h"

#include "logger.h"

namespace util {
uint32_t task_stack_unused_bytes() {
  return sizeof(StackType_t) * uxTaskGetStackHighWaterMark(NULL);
}

// void dump_heap_stats() {
//   logger.info("Heap: Total=%d, free=%d, free_min=%d", configTOTAL_HEAP_SIZE,
//               xPortGetFreeHeapSize(), xPortGetMinimumEverFreeHeapSize());
// }
}  // namespace util