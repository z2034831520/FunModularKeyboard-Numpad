#include "ec11.h"


EC11_t EC11_1, EC11_2, EC11_3;

void EC11_GPIO_Init()
{
 /* ----------------- 初始化GPIO ----------------- */
    /* EC11 1: PA1/PA2/PD4 */
    PA_DDR &= ~(1<<1); PA_CR1 |= (1<<1); PA_CR2 &= ~(1<<1);
    PA_DDR &= ~(1<<2); PA_CR1 |= (1<<2); PA_CR2 &= ~(1<<2);
    PD_DDR &= ~(1<<4); PD_CR1 |= (1<<4); PD_CR2 &= ~(1<<4);

    /* EC11 2: PD3/PC7/PA3 */
    PD_DDR &= ~(1<<3); PD_CR1 |= (1<<3); PD_CR2 &= ~(1<<3);
    PC_DDR &= ~(1<<7); PC_CR1 |= (1<<7); PC_CR2 &= ~(1<<7);
    PA_DDR &= ~(1<<3); PA_CR1 |= (1<<3); PA_CR2 &= ~(1<<3);

    /* EC11 3: PC3/PC5/PC6 */
    PC_DDR &= ~(1<<3); PC_CR1 |= (1<<3); PC_CR2 &= ~(1<<3);
    PC_DDR &= ~(1<<5); PC_CR1 |= (1<<5); PC_CR2 &= ~(1<<5);
    PC_DDR &= ~(1<<6); PC_CR1 |= (1<<6); PC_CR2 &= ~(1<<6);    
    

    /* ----------------- 初始化结构体 ----------------- */
    EC11_1.port_a_idr = &PA_IDR; EC11_1.pin_a = 1;
    EC11_1.port_b_idr = &PA_IDR; EC11_1.pin_b = 2;
    EC11_1.port_sw_idr = &PD_IDR; EC11_1.pin_sw = 4;
    EC11_Init(&EC11_1);

    EC11_2.port_a_idr = &PD_IDR; EC11_2.pin_a = 3;
    EC11_2.port_b_idr = &PC_IDR; EC11_2.pin_b = 7;
    EC11_2.port_sw_idr = &PA_IDR; EC11_2.pin_sw = 3;
    EC11_Init(&EC11_2);

    EC11_3.port_a_idr = &PC_IDR; EC11_3.pin_a = 3;
    EC11_3.port_b_idr = &PC_IDR; EC11_3.pin_b = 5;
    EC11_3.port_sw_idr = &PC_IDR; EC11_3.pin_sw = 6;
    EC11_Init(&EC11_3);
}

/* 初始化结构体 */
void EC11_Init(EC11_t *ec11)
{
    ec11->encoder_cnt = 0;
    ec11->encoder_dir = 0;
    ec11->acc = 0;
    ec11->last_state = 0;
    ec11->dir_timer = 0;

    ec11->state = IDLE;
    ec11->counter = 0;
    ec11->event = 0;
}

/* 旋钮轮询，带10ms自动清零encoder_dir */
void EC11_Process(EC11_t *ec11)
{
    uint8_t cur_state = 0;

    if (*(ec11->port_a_idr) & (1 << ec11->pin_a)) cur_state |= 0x02;
    if (*(ec11->port_b_idr) & (1 << ec11->pin_b)) cur_state |= 0x01;

    if (ec11->last_state == 0x00)
    {
        if (cur_state == 0x01) // B先置位
        {
            ec11->encoder_cnt--;
            ec11->encoder_dir = 1; // CCW
            ec11->dir_timer = 0;
        }
        else if (cur_state == 0x02) // A先置位
        {
            ec11->encoder_cnt++;
            ec11->encoder_dir = 2; // CW
            ec11->dir_timer = 0;
        }
    }

    ec11->last_state = cur_state;

    /* 10ms超时清零 */
    if (ec11->encoder_dir != 0)
    {
        ec11->dir_timer++;
        if (ec11->dir_timer >= 10) // 假设轮询周期1ms
        {
            ec11->encoder_dir = 0;
            ec11->dir_timer = 0;
        }
    }
}

/* 按键状态机 */
void EC11_KeyProcess(EC11_t *ec11)
{
    uint8_t pressed = ((*(ec11->port_sw_idr) & (1 << ec11->pin_sw)) == 0);

    switch (ec11->state)
    {
        case IDLE:
            if (pressed)
            {
                ec11->state = DEBOUNCE_PRESS;
                ec11->counter = 0;
            }
            break;

        case DEBOUNCE_PRESS:
            if (pressed)
            {
                if (++ec11->counter >= KEY_DEBOUNCE_MS)
                {
                    ec11->state = PRESSED;
                    ec11->event = 1;
                }
            }
            else
            {
                ec11->state = IDLE;
            }
            break;

        case PRESSED:
            if (!pressed)
            {
                ec11->state = DEBOUNCE_RELEASE;
                ec11->counter = 0;
            }
            break;

        case DEBOUNCE_RELEASE:
            if (!pressed)
            {
                if (++ec11->counter >= KEY_DEBOUNCE_MS)
                    ec11->state = IDLE;
            }
            else
            {
                ec11->state = PRESSED;
            }
            break;

        default:
            ec11->state = IDLE;
            break;
    }
}

/* 返回状态机状态 */
uint8_t EC11_KeyRead(EC11_t *ec11)
{
    return ec11->state;
}
