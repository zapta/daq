#include "cdc_serial.h"

#include <algorithm>
#include <cstring>

#include "FreeRTOS.h"
#include "circular_queue.h"
#include "main.h"
#include "semphr.h"
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

// A circual queue of pending bytes.
static constexpr uint16_t kBufferSize = 1000;
static uint8_t buffer[kBufferSize];
static CircularQueue<uint8_t> queue(buffer, kBufferSize);

// Semaphore to protect access to the buffer.
static SemaphoreHandle_t semaphore_handle = nullptr;

// Temp buffer for sending data.
static uint8_t tx_buffer[100];

static void tx_task(void* argument) {
  for (;;) {
    // Transfer a chunk of data to tx_buffer, if avaiable.
    uint16_t bytes_to_send = 0;
    xSemaphoreTake(semaphore_handle, portMAX_DELAY);
    { bytes_to_send = queue.dequeue(tx_buffer, sizeof(tx_buffer)); }
    xSemaphoreGive(semaphore_handle);

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

  semaphore_handle = xSemaphoreCreateBinary();
  if (!semaphore_handle) {
    Error_Handler();
  }
  xSemaphoreGive(semaphore_handle);

  TaskHandle_t task_handle = NULL;
  xTaskCreate(tx_task, "Logger", 500 / sizeof(StackType_t), nullptr, 10,
              &task_handle);
  if (!task_handle) {
    Error_Handler();
  }
}

void write(const uint8_t* bfr, uint16_t len) {
  xSemaphoreTake(semaphore_handle, portMAX_DELAY);
  { queue.enqueue(bfr, len); }
  xSemaphoreGive(semaphore_handle);
}

}  // namespace cdc_serial
