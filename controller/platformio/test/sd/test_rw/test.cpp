// Unit test SD hardware read/write

#include <FreeRTOS.h>
#include <task.h>
#include <unity.h>

#include <memory>
#include <vector>

#include "../../unity_util.h"
#include "fatfs.h"
#include "serial_packets_data.h"
#include "text_util.h"
#include "time_util.h"

constexpr uint32_t kBytesToTest = 10000000;
constexpr uint32_t kBytesPerPacket = 720;
static uint8_t buffer[kBytesPerPacket];
static StuffedPacketBuffer stuffed_packet;

static TCHAR file1_wname[20];

void setUp() {
  static_assert(sizeof(uint16_t) == sizeof(TCHAR));
  const bool ok = text_util::wstr_from_str(
      file1_wname, sizeof(file1_wname) / sizeof(file1_wname[0]), "TEST.BIN");
  TEST_ASSERT_TRUE(ok);
}

void tearDown() {}

void test_read_write() {
  volatile FRESULT status = f_mount(&SDFatFS, (TCHAR const*)SDPath, 0);
  TEST_ASSERT_EQUAL(status, FRESULT::FR_OK);

  // ----- Write file
  status = f_open(&SDFile, file1_wname, FA_CREATE_ALWAYS | FA_WRITE);
  TEST_ASSERT_EQUAL(status, FRESULT::FR_OK);

  uint32_t bytes_written = 0;

  static_assert(kBytesPerPacket % sizeof(uint32_t) == 0);
  while (bytes_written < kBytesToTest) {
    stuffed_packet.clear();
    while (stuffed_packet.size() < kBytesPerPacket &&
           !stuffed_packet.had_read_errors() && bytes_written < kBytesToTest) {
      stuffed_packet.write_uint32(bytes_written);
      bytes_written += 4;
    }
    TEST_ASSERT_FALSE(stuffed_packet.had_write_errors());
    const uint32_t n = stuffed_packet.size();
    stuffed_packet.read_bytes(buffer, n);
    TEST_ASSERT_FALSE(stuffed_packet.had_read_errors());
    unsigned int bytes_written;
    status = f_write(&SDFile, buffer, n, &bytes_written);
    TEST_ASSERT_EQUAL(status, FRESULT::FR_OK);
    TEST_ASSERT_EQUAL(bytes_written, n);

    status = f_sync(&SDFile);
    TEST_ASSERT_EQUAL(status, FRESULT::FR_OK);
  }

  f_close(&SDFile);

  // ----- Write file

  status = f_open(&SDFile, file1_wname, FA_OPEN_EXISTING | FA_READ);
  TEST_ASSERT_EQUAL(status, FRESULT::FR_OK);

  uint32_t bytes_verified = 0;
  while (bytes_verified < kBytesToTest) {
    static_assert(sizeof(buffer) >= kBytesPerPacket);
    uint32_t n = std::min(kBytesPerPacket, (kBytesToTest - bytes_verified));
    unsigned int bytes_read;
    status = f_read(&SDFile, buffer, n, &bytes_read);
    TEST_ASSERT_EQUAL(status, FRESULT::FR_OK);
    TEST_ASSERT_EQUAL(bytes_read, n);

    stuffed_packet.clear();
    stuffed_packet.write_bytes(buffer, n);
    TEST_ASSERT_FALSE(stuffed_packet.had_write_errors());
    while (!stuffed_packet.all_read() && !stuffed_packet.had_read_errors()) {
      const uint32_t v = stuffed_packet.read_uint32();
      TEST_ASSERT_FALSE(stuffed_packet.had_read_errors());
      TEST_ASSERT_EQUAL(v, bytes_verified);
      bytes_verified += 4;
    }
    TEST_ASSERT_TRUE(stuffed_packet.all_read_ok());
  }

  f_close(&SDFile);

  f_mount(&SDFatFS, (TCHAR const*)NULL, 0);
}

void app_main() {
  unity_util::common_start();

  UNITY_BEGIN();
  RUN_TEST(test_read_write);
  UNITY_END();

  unity_util::common_end();
}
