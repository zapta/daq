#include "inttypes.h"

void SysTick_Handler(void);

void MyTrap() {
  // uint32_t out;
  // asm("mrs r0,ipsr");
  // asm("mov r0, %0" : "=r" (out));

  for (;;) {
    asm("nop");
  }
}

// static __attribute__((used)) int var1;
// uint32_t get_ipsr(void)
// {
// 	uint32_t out;
// 	asm("mrs r0,ipsr");
//   asm("mov r0, %0" : "=r" (out));

// 	// return out;
//   return out;
// }

// void My_Default_Handler() {
//   // const uint32_t ipsr = get_ipsr();
//   MyTrap();
// }

// void My_Default_Handler_2() {
//   // const uint32_t ipsr = get_ipsr();
//   MyTrap();
// }

// void My_Default_Handler_3() {
//   // const uint32_t ipsr = get_ipsr();
//   MyTrap();
// }
// void xxvPortSVCHandler( void );

// void SVC_Handler(void) {
//   xxvPortSVCHandler(  );
// }

// void My_Default_Handler_4() {
//   SVC_Handler();
//   // const uint32_t ipsr = get_ipsr();
//   MyTrap();
// }

// void SVC_Handler(void);
