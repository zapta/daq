#include <Arduino.h>
#include <STM32FreeRTOS.h>

#include "cmsis_os.h"
#include "task.h"

 #include "io.h"

extern "C" {
extern const int uxTopUsedPriority;
// __attribute__((section(".rodata")))
const int uxTopUsedPriority = configMAX_PRIORITIES - 1;
}

void main_task(void* /*argument*/) {
  __HAL_RCC_GPIOE_CLK_ENABLE();
  io::LED.init();
    // pinMode(LED_BUILTIN, OUTPUT);
      // digitalWrite(LED_BUILTIN, HIGH);

  // tid_phaseA = osThreadNew(phaseA, NULL, NULL);
  // tid_phaseB = osThreadNew(phaseB, NULL, NULL);
  // tid_phaseC = osThreadNew(phaseC, NULL, NULL);
  // tid_phaseD = osThreadNew(phaseD, NULL, NULL);
  // tid_clock  = osThreadNew(clock,  NULL, NULL);

  // osThreadFlagsSet(tid_phaseA, 0x0001); /* set signal to phaseA thread   */
  for (;;) {
    io::LED.toggle();
    // digitalWrite(LED_BUILTIN, HIGH);
    osDelay(100);
    // digitalWrite(LED_BUILTIN, LOW);
    // osDelay(100);
  }
}

void setup() {
  TaskHandle_t xHandle = NULL;
  xTaskCreate(main_task, "Main", 1000 / sizeof(StackType_t),(void*) &uxTopUsedPriority , 10,
              &xHandle);
  // Normally, this never returns.
  vTaskStartScheduler();

  // pinMode(LED_BUILTIN, OUTPUT);
  // // digitalWrite(LED_BUILTIN, HIGH);
  // osKernelInitialize();  // Initialize CMSIS-RTOS
  // osThreadNew(main_task, NULL,
  //             NULL);  // Create application main thread
  // if (osKernelGetState() == osKernelReady) {
  //   osKernelStart();  // Start thread execution
  // }

  while (1) {

  }
    
}

void loop() {}
