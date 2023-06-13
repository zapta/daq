
#include "unity_config.h"

#include "cdc_serial.h"

void unityOutputStart() {}

void unityOutputChar(char c) { cdc_serial::write((const uint8_t*)&c, 1); }

void unityOutputFlush() {}

void unityOutputComplete() {}