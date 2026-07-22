#include "iostm8s103f3.h"
#include <stdint.h>
#include <intrinsics.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "Uart.h"
#include "common.h"

#include "i2c.h"
#include "ec11.h"
#include "adc.h"


// 增加一个计数变量来观察旋转
//static int16_t rotation_counter = 0;

void i2c_user_Slave_Rx_Callback(BYTE *userdata,BYTE size)
{
    // 原代码保持不变
}

void i2c_user_Slave_Tx_Callback(BYTE *userdata,BYTE size)
{
    // 原代码保持不变
}

void i2c_user_Error_Callback(BYTE l_sr2)
{
    // 原代码保持不变
}

int main( void )
{
    BYTE slaveRxData[32] = {0};  // 从机接收缓冲区
    BYTE slaveTxData[128] = {0};  // 从机发送缓冲区
    BYTE counter = 0;
    uint16_t slider1_val, slider2_val;
    //char temp[64]={0};
    
    // 初始化发送缓冲区数据
    // sprintf(slaveTxData, "I2C Slave Response");
    
    InitialiseSystemClock();
    InitialiseUART();
    
    UARTPrintf("\r\n_________________________\n\r");
    UARTPrintf("FunModularKeyboard_ModA V1.0.0\n\r");
    
    // 初始化EC11
    EC11_GPIO_Init();
    
    // 初始化为从机
    I2C_Init();  
    
    // 使用分开的缓冲区配置从机
    I2C_Slave_Configure(0x06, slaveRxData, 32, slaveTxData, 128);
    
    // 初始化ADC
    ADC_GPIO_Init();
    ADC_Init();
    
    __enable_interrupt();
    
    while (1)
    {
      
       // 轮询每个旋钮
        EC11_Process(&EC11_1);
        EC11_KeyProcess(&EC11_1);

        EC11_Process(&EC11_2);
        EC11_KeyProcess(&EC11_2);

        EC11_Process(&EC11_3);
        EC11_KeyProcess(&EC11_3);

        // 读取EC11状态
        uint8_t ec111_encoder_dir = EC11_1.encoder_dir;
        uint8_t ec112_encoder_dir = EC11_2.encoder_dir;
        uint8_t ec113_encoder_dir = EC11_3.encoder_dir;
        uint8_t sw1 = EC11_KeyRead(&EC11_1);
        uint8_t sw2 = EC11_KeyRead(&EC11_2);
        uint8_t sw3 = EC11_KeyRead(&EC11_3);
      
        // 读取slider(adc)状态
        ReadVol_CHx(&slider2_val, &slider1_val);
      
        // 处理接收到的数据（如果有）
       if (slaveRxData[0] != 0) {
            // 原代码保持不变
            counter++;
            //sprintf((char *)slaveTxData, "[I2C_RESPONSE]MODA:Slider1:[0][1000][50],Slider2:[0][1000][150],Knob1:[0][0],Knob2:[0][0],Knob3:[0][0],index=%d/over", counter);
            sprintf((char *)slaveTxData, "[I2C_RESPONSE]MODA:Slider1:[0][1024][%d],Slider2:[0][1025][%d],Knob1:[%d][%d],Knob2:[%d][%d],Knob3:[%d][%d],index=%d/over",slider1_val, slider2_val, ec111_encoder_dir, sw1, ec112_encoder_dir, sw2, ec113_encoder_dir, sw3, counter);
        }
        delay_1ms_Count(1);
    }
}



/*
void main(void)
{
    char temp[64]={0};
  
    uint16_t ain2_val, ain3_val;

    InitialiseSystemClock();
    InitialiseUART();
    
    ADC_GPIO_Init();
    ADC_Init();

    while (1)
    {
        ReadVol_CHx(&ain2_val, &ain3_val);
        // adc_value 即滑动变阻器数值：0 ~ 1023 
        sprintf(temp, "ain2_val=%d,ain3_val=%d", ain2_val,ain3_val);
        UARTPrintfLn(temp);
        
        delay_1ms_Count(100);
    }
}*/