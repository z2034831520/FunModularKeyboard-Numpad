#ifndef __EC11_H
#define __EC11_H

#include <stdint.h>
#include <iostm8s103f3.h>

#define KEY_DEBOUNCE_MS 10

typedef enum {
    IDLE = 0,
    DEBOUNCE_PRESS,
    PRESSED,
    DEBOUNCE_RELEASE
} KeyState_t;

typedef struct {
    KeyState_t state;
    uint16_t counter;
    uint8_t event;

    const volatile uint8_t *port_a_idr;
    uint8_t pin_a;
    const volatile uint8_t *port_b_idr;
    uint8_t pin_b;
    const volatile uint8_t *port_sw_idr;
    uint8_t pin_sw;

    int16_t encoder_cnt;
    uint8_t encoder_dir;  // 0: IDLE, 1: CCW, 2: CW
    uint8_t acc;
    uint8_t last_state;

    uint8_t dir_timer;    //      10ms      ʱ  
} EC11_t;

extern EC11_t EC11_1, EC11_2, EC11_3;

void EC11_GPIO_Init();
void EC11_Init(EC11_t *ec11);
void EC11_Process(EC11_t *ec11);
void EC11_KeyProcess(EC11_t *ec11);
uint8_t EC11_KeyRead(EC11_t *ec11);

#endif




