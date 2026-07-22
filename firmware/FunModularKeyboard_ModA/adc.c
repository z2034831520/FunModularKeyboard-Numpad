#include "adc.h"


void ADC_GPIO_Init(void)
{
    // PD2 输入浮空
    PD_DDR &= ~(1 << 2);
    PD_CR1 &= ~(1 << 2);
    PD_CR2 &= ~(1 << 2);

    // PC4 输入浮空
    PC_DDR &= ~(1 << 4);
    PC_CR1 &= ~(1 << 4);
    PC_CR2 &= ~(1 << 4);
}

/*
单次读取模式
void ADC1_Init()
{
    ADC_CR1 = 0x02; //连续转换模式 
    ADC_CR2|=0x0a; // 数据排列,右对齐,使能扫描模式
    ADC_CR3 = 0x00;  // 默认低速采样
    //ADC_CR1 |= 0x01;  //启动 ADC
}

uint16_t ADC1_ReadChannel(uint8_t channel)
{
    uint8_t high, low;
    uint16_t value;

    //选择通道（保留高位状态） //
    ADC_CSR = (ADC_CSR & 0xE0) | channel;

    //---------- real conversion ---------- //
    // 启动转换
    ADC_CR1 |= 0x01;
   // 延时一段时间，至少7uS，保证ADC 模块上电王城
    delay_10us();
    // 再次将CR1寄存器的最低位置1,开启AD转换
    ADC_CR1|=0x01;      
    // 等待转换完成
    while((ADC_CSR & 0x80)!=0x80); // 等待转换结束

    // 读取 ADC 数据（分开读取避免 Pa082）
    high = ADC_DRH;
    low  = ADC_DRL;

     // 清除 EOC
    ADC_CSR&=(~0x80);
    
    value = ((uint16_t)high << 8) | low;

    return value;
}*/


//连续扫描模式
//ch 为ADC通道 连续转换AIN0---AINch 通道的数据
void ADC_Init()
{
    char l = 0;

    ADC_GPIO_Init();
    
    ADC_CR1 &= ~( 7 << 4 );   //预分频 2
    ADC_CR2 &= ~( 1 << 6 );   //不使用外部触发
    //禁止 AIN2 AIN3 的施密特触发器，降低 IO 静态功耗
    ADC_TDRL |= ( 1 << 2 );
    ADC_TDRL |= ( 1 << 3 );

    ADC_CR1 |= ( 1 << 1 );   //连续转换
    ADC_CSR |= 0x03;          //配置通道号最大的那个
    ADC_CR2 |= ( 1 << 3 );    //右对齐

    ADC_CR1 |= ( 1 << 0 );    //开启 ADC
    ADC_CR2 |= ( 1 << 1 );    // SCAN = 1 开启扫描模式

      //当首次置位ADON位时，ADC从低功耗模式唤醒。为了启动转换必须第二次使用写指令来置位ADC_CR1寄存器的ADON位。
    for( l = 0; l < 10; l++ );  //延时，保证ADC模块的上电完成 至少7us
    ADC_CR1 |= ( 1 << 0 );      //再次将CR1寄存器的最低位置１ 使能ADC 并开始转换
}

/*
注意：在扫描模式（连续扫描模式）中，不要使用位操作指令(BRES)去清除EOC标志位，
这是因为该指令是对整个ADC_CSR寄存器的一个读-修改-写操作。
从CH[3:0]寄存器中读取当前的通道编号和写回该寄存器，将会改变扫描系列的最后通道编号。
在连续扫描模式中正确的清除EOC标志位的方法是 个RAM变量中载入一个字节到ADC_CSR寄存器，
这样来清除EOC标志位同时还重新载入扫描系列新的最后通道编号。

实验发现，位操作指令只在连续扫描模式中会清除CH[3:0]寄存器中的值，但并不影响其他值。
因此将ADC_CSR中的值读出，再将CH[3:0]中原来通道号加入进去，最后重新写入ADC_CSR中即可。写法如下：

ADC1->CSR = (uint8_t)(ADC1->CSR &(~ADC1_FLAG_EOC)|ADC1_CHANNEL_n);

注：ADC1_CHANNEL_n表示扫描到那个通道结束。

*/
//读取采样电压值
void ReadVol_CHx(uint16_t *ain2_val, uint16_t *ain3_val)
{
    uint16_t temph = 0;
    uint8_t templ = 0;

    while( ( ADC_CSR & 0x80 ) == 0 );      //等待转换结束
    //ADC_CSR &= ~( 1 << 7 );               // 不能通过位操作来清零  EOC 标志
    
    ADC_CSR = ADC_CSR & 0x7F | 0x04;        // 转换结束标志位清零  EOC

    //读取 AIN2 的值
    templ = ADC_DB2RL;
    temph = ADC_DB2RH;
    temph = (uint16_t)( templ | (uint16_t)( temph << (uint16_t)8 ) );
    *ain2_val =  temph;
    
    //读取 AIN3 的值
    templ = ADC_DB3RL;
    temph = ADC_DB3RH;
    temph = (uint16_t)( templ | (uint16_t)( temph << (uint16_t)8 ) );
    *ain3_val =  temph;
}

