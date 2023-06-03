#include "util.h"

namespace util {
uint32_t task_stack_unused_bytes() {
return         sizeof(StackType_t) * uxTaskGetStackHighWaterMark(NULL);

}
}// namespace util