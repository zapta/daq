
#include "main.h"

#include "FreeRTOS.h"
// #include "cmsis_os.h"
#include "gpio.h"
#include "task.h"
#include "usart.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"

// extern "C" {
//  extern int _write(int file, uint8_t* p, int len);

// int _write(int file, uint8_t* p, int len) {
//   while (CDC_Transmit_FS(p, len) == USBD_BUSY);
//   return len;
// }
// }

void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
   */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
   */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
  }

  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
  }

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                                RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK) {
    Error_Handler();
  }
}

// static osThreadId_t defaultTaskHandle;

// extern void MX_USB_DEVICE_Init(void);

void defaultTask(void *argument) {
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
  // const int f_cpu = F_CPU;
  /* init code for USB_DEVICE */
  for (;;) {
    uint8_t aa[] = "aaa\n";
    CDC_Transmit_FS(aa, sizeof(aa));
    // osDelay(1);
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    vTaskDelay(600);
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
    vTaskDelay(100);
    // printf("Hello world\n");
  }
}

extern "C" {
extern const int uxTopUsedPriority;
__attribute__((section(".rodata"))) const int uxTopUsedPriority =
    configMAX_PRIORITIES - 1;
}

// __attribute__((used)) void keep() {
//   printf("%p\n", &uxTopUsedPriority);
// }

int main(void) {
  // HAL_Init();
  // SystemClock_Config();
  // MX_GPIO_Init();
  // MX_USART1_UART_Init();

  // MX_USB_DEVICE_Init();

  // vTaskStartScheduler();

  //--------------

  HAL_Init();

  // int a = 4;
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  MX_USB_DEVICE_Init();
  // CDC_Init_FS();

  //  for(;;) {
  //	  vTaskDelay(10);
  //  }

  /* Call init function for freertos objects (in freertos.c) */
  //  MX_FREERTOS_Init();

  /* Start scheduler */
  //  osKernelStart();

  // vTaskStartScheduler();

  // for(;;) {
  //     //  printf("%p\n", &uxTopUsedPriority);
  //     uint8_t aa[] = "aaa\n";
  //      CDC_Transmit_FS(aa, sizeof(aa));

  //   vTaskDelay(100);
  // }

  //------------

  // for(;;) {
  //   vTaskDelay(600);
  // }

  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);

  // osKernelInitialize();

  TaskHandle_t xHandle = NULL;
  xTaskCreate(defaultTask, "T1", 1000 / sizeof(StackType_t), nullptr, 10,
              &xHandle);
  // xTaskCreate(defaultTask, "T2", 1000 / sizeof(StackType_t), nullptr, 10,
  //             &xHandle);

  vTaskStartScheduler();

  // This should be non reachable.
  Error_Handler();
}

void Error_Handler(void) {
  __disable_irq();
  while (1) {
  }
}
