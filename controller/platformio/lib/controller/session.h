
#pragma once

#include "common.h"

namespace session {

// Call once to initialize the id.
void setup();

// Return session id. Stable throughotu the life
// time of this run.
uint32_t id();

} // namespace session

