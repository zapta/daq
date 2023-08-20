#include "gpio_pins.h"

namespace gpio_pins {

// Outputs
OutputPin LED(LED_GPIO_Port, LED_Pin, 0, 0);
OutputPin TEST1(TEST1_GPIO_Port, TEST1_Pin, 0, 0);

// Inputs
InputPin USER_SWITCH(USER_SWITCH_GPIO_Port, USER_SWITCH_Pin);
InputPin SD_SWITCH(SD_SWITCH_GPIO_Port, SD_SWITCH_Pin);

}  // namespace gpio_pins.