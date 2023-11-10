
// Contains common declaration that are useful to virtually any file.

#pragma once

#include "error_handler.h"
#include "main.h"
#include <stdint.h>
#include "logger.h"
#include "time_util.h"

#define MUST_USE_VALUE __attribute__((warn_unused_result))
