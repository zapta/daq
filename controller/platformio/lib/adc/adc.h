#pragma once

class Adc {
 public:
 private:
};
namespace adc {

// void test_setup();
// void test_loop();

  // Does not return.
  void adc_task_body(void* argument);

  void dump_state();

  // For diagnostics.
  void verify_registers_vals();

}  // namespace adc
