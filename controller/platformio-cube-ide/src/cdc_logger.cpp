#include "cdc_logger.h"

#include <algorithm>
#include <cstring>

#include "FreeRTOS.h"
#include "main.h"
#include "semphr.h"
#include "task.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "circular_byte_buffer.h"



namespace cdc_logger {

static uint8_t buffer[1000];
static CircularByteBuffer queue(buffer, sizeof(buffer)) ;

// Number of bytes waiting in the buffer. <= sizeof(queue).
// static uint16_t queue_data_size = 0;
// Index of oldest byte, if any. < sizeof(queue);
// static uint16_t queue_data_start = 0;
static SemaphoreHandle_t semaphore_handle = nullptr;

static uint8_t tx_buffer[100];

// static inline void normalize(uint16_t& v) {
//   if (v >= sizeof(queue)) {
//     v -= sizeof(queue);
//   }
// }


static void logger_task(void* argument) {
  for (;;) {
    // Transfer a chunk of data to tx_buffer, if avaiable.
    uint16_t bytes_to_send = 0;
    xSemaphoreTake(semaphore_handle, portMAX_DELAY);
    {
      bytes_to_send = queue.dequeue(tx_buffer, sizeof(tx_buffer));
      // const uint16_t limit1 = queue_data_size;
      // const uint16_t limit2 = sizeof(queue) - queue_data_start;
      // bytes_to_send = std::min(limit1, limit2);
      // if (bytes_to_send) {
      //   memcpy(tx_buffer, &queue[queue_data_start], bytes_to_send);
      //   queue_data_size -= bytes_to_send;
      //   queue_data_start += bytes_to_send;
      //   normalize(queue_data_start);
      // }
    }
    xSemaphoreGive(semaphore_handle);

    // Transmit the data in tx_buffer, if any. This is
    // done outside of the semaphore, so other task can push
    // more data.
    if (bytes_to_send) {
      // Blocking sending to
      // uint8_t rc = USBD_FAIL;
      for (;;) {
        const uint8_t rc = CDC_Transmit_FS(tx_buffer, bytes_to_send);
        // Exit on error or ok.
        if (rc != USBD_BUSY) {
          break;
        }
        vTaskDelay(1);
      }
    } else {
      // Nothign to transmit. Long delay.
      vTaskDelay(50);
    }
  }
}


void setup() {
    MX_USB_DEVICE_Init();
  // Let the USB connection stabalize.
  HAL_Delay(500);

  semaphore_handle = xSemaphoreCreateBinary();
  xSemaphoreGive(semaphore_handle);

    // printf("CDC Logger setup\n");


  TaskHandle_t task_handle = NULL;
  xTaskCreate(logger_task, "Logger", 500 / sizeof(StackType_t), nullptr, 10,
              &task_handle);
  if (!task_handle) {
    Error_Handler();
  }
}



void write(const uint8_t* bfr, uint16_t len) {
  // We don't expect this to block for too long.
  xSemaphoreTake(semaphore_handle, portMAX_DELAY);
  {
    queue.enqueue(bfr, len);
    // uint16_t bytes_added = 0;
    // // On each iteration either add a chunk or if queue is full
    // // free a chunk.
    // while (bytes_added < len) {
    //   const uint16_t bytes_left_to_add = len - bytes_added;
    //   // If queue is full, free the oldest bytes.
    //   if (queue_data_size >= sizeof(queue)) {
    //     const uint16_t limit1 = bytes_left_to_add;
    //     const uint16_t limit2 = sizeof(queue) - queue_data_start;
    //     const uint16_t bytes_to_drop = std::min(limit1, limit2);
    //     queue_data_size -= bytes_to_drop;
    //     queue_data_start += bytes_to_drop;
    //     normalize(queue_data_start);
    //   } else {
    //     uint16_t dst = queue_data_start + queue_data_size;
    //     normalize(dst);
    //     const uint16_t limit1 = bytes_left_to_add;
    //     const uint16_t limit2 = (dst < queue_data_start)
    //                                 ? queue_data_start - dst
    //                                 : sizeof(queue) - dst;
    //     const uint16_t bytes_to_add = std::min(limit1, limit2);
    //     memcpy(&queue[dst], &bfr[bytes_added], bytes_to_add);
    //     queue_data_size += bytes_to_add;
    //     bytes_added += bytes_to_add;                       
    //   }
    // }
  }
  xSemaphoreGive(semaphore_handle);
}

}  // namespace cdc_logger

// TODO: Check the status returned from the semaphore functions.
extern "C" {
extern int _write(int, uint8_t *, int);
int _write(int file, uint8_t *ptr, int len) {
  cdc_logger::write(ptr, len);
  return len;
  // if ((file != STDOUT_FILENO) && (file != STDERR_FILENO)) {
  //   return -1;
  // }
  // uint8_t rc = USBD_FAIL;
  // do {
  //   rc = CDC_Transmit_FS(ptr, (uint16_t)len);
  // } while (rc == USBD_BUSY);

  // if (rc != USBD_OK) {
  //   return -1;
  // }
  // return len;
// }
}
}
