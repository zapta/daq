#include "io.h"

namespace io {

  // Outputs
  OutputPin LED(LED_GPIO_Port, LED_Pin, 0, 0);
  OutputPin TEST1(TEST1_GPIO_Port, TEST1_Pin, 0, 0);

  // Inputs
   InputPin USER_SWITCH(USER_SWITCH_GPIO_Port, USER_SWITCH_Pin);

  
}  // namespace io.