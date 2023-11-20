#pragma once

#include <FreeRTOS.h>

#include "common.h"

// Protects the rest of this scope as a criticla section.
// This prevents ISRs and context switches. Do not use
// in an ISR, but from tasks only.
class CritialSectionScope {
 public:
  inline CritialSectionScope() {
    // Enter the critical section.
    taskENTER_CRITICAL();
  }
  inline ~CritialSectionScope() {
    // Exit the critical section.
    taskEXIT_CRITICAL();
  }

  // Prevent copy and assignment.
  CritialSectionScope(const CritialSectionScope& other) = delete;
  CritialSectionScope& operator=(const CritialSectionScope& other) = delete;
};

#define __CRITICAL_SECTION_TILL_END_OF_SCOPE__ CritialSectionScope _critical_section_scope