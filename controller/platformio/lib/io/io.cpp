#include "io.h"

namespace io {

  // Outputs
  OutputPin LED(LED_GPIO_Port, LED_Pin, 0, 0);
  OutputPin TEST1(TEST1_GPIO_Port, TEST1_Pin, 0, 0);
  OutputPin TEST2(TEST2_GPIO_Port, TEST2_Pin, 0, 0);
  // OutputPin TEST3(TEST3_GPIO_Port, TEST3_Pin, 0, 0);
  OutputPin ADC_CS(ADC_CS_GPIO_Port, ADC_CS_Pin, 0, 1);


  // Inputs
   InputPin SWITCH(SWITCH_GPIO_Port, SWITCH_Pin);

  
}  // namespace io.