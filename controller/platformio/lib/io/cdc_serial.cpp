#include "cdc_serial.h"

#include <algorithm>
#include <cstring>

#include "FreeRTOS.h"
#include "circular_buffer.h"
#include "main.h"
#include "semphr.h"
#include "static_mutex.h"
#include "static_task.h"
#include "task.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"

// An helper for printf(). Do not printf() before calling setup() here.
// TODO: Check the status returned from the semaphore functions.
extern "C" {
extern int _write(int, uint8_t*, int);
int _write(int file, uint8_t* ptr, int len) {
  cdc_serial::write(ptr, len);
  return len;
}
}

namespace cdc_serial {

// A circual buffer of pending bytes.
// static constexpr uint16_t kBufferSize = 1000;
// static uint8_t buffer[kBufferSize];
static CircularBuffer<uint8_t, 1000> circular_buffer;

// Semaphore to protect access to the buffer.
// static SemaphoreHandle_t semaphore_handle = nullptr;
static StaticMutex mutex;

void tx_task_body(void* argument);
static StaticTask<1000> task(tx_task_body, "Logger", 10);

// static StaticTask<1000

// Temp buffer for sending data.
static uint8_t tx_buffer[100];

void tx_task_body(void* argument) {
  for (;;) {
    // Transfer a chunk of data to tx_buffer, if avaiable.
    uint16_t bytes_to_send = 0;
    // xSemaphoreTake(semaphore_handle, portMAX_DELAY);
    {
      MutexScope mutex_scope(mutex);
      bytes_to_send = circular_buffer.read(tx_buffer, sizeof(tx_buffer));
    }
    // xSemaphoreGive(semaphore_handle);

    if (bytes_to_send) {
      for (;;) {
        const uint8_t rc = CDC_Transmit_FS(tx_buffer, bytes_to_send);
        // Exit on error or ok.
        if (rc != USBD_BUSY) {
          break;
        }
        vTaskDelay(1);
      }
    } else {
      // Nothign to transmit. Wait a little.
      vTaskDelay(50);
    }
  }
}

// static uint8_t empty_buffer[] = {"."};

void setup() {
  MX_USB_DEVICE_Init();
  // Let the USB driver stablize, otherwise we loose initial
  // data (which is not a big deal);
  HAL_Delay(1000);

  if (!task.start()) {
    Error_Handler();
  }

  // semaphore_handle = xSemaphoreCreateBinary();
  // if (!semaphore_handle) {
  //   Error_Handler();
  // }
  // xSemaphoreGive(semaphore_handle);

  // TaskHandle_t task_handle = NULL;
  // xTaskCreate(tx_task, "Logger", 500 / sizeof(StackType_t), nullptr, 10,
  //             &task_handle);
  // if (!task_handle) {
  //   Error_Handler();
  // }
}

void write_str(const char* str) {
  const uint16_t len = strlen(str);
  write((uint8_t*)str, len);
}

void write(const uint8_t* bfr, uint16_t len) {
  // xSemaphoreTake(semaphore_handle, portMAX_DELAY);
  {
    MutexScope mutex_scope(mutex);
    circular_buffer.write(bfr, len, true);
  }
  // xSemaphoreGive(semaphore_handle);
}

}  // namespace cdc_serial
