#include "i2c.h"
#include "Uart.h"

#include <iostm8s103f3.h>

struct i2c_s {
    BYTE    SlaveAddress;
    BYTE*   masterBuffer;
    BYTE    masterTransactionLength;
    BYTE    buffer_index;
    BYTE    reg;
    BYTE    masterMode;
    BYTE    readwrite;
    
    // 分开的从机缓冲区
    BYTE*   slaveRxBuffer;      // 接收缓冲区
    BYTE    slaveRxLength;      // 接收缓冲区大小
    BYTE*   slaveTxBuffer;      // 发送缓冲区
    BYTE    slaveTxLength;      // 发送缓冲区大小
    
} i2c;

//------------------------------------------------------------------------------------------------------------------
//					I2C Registers
//------------------------------------------------------------------------------------------------------------------
//	I2C_CR1		Control Register 1
//	I2C_CR2		Control Register 2
//	I2C_FREQR	Frequency Register
//	I2C_OARL	Own Address Register LSB
//	I2C_OARH	Own Address Register MSB
//	I2C_DR		Data Register
//	I2C_SR1		Status Register 1
//	I2C_SR2		Status Register 2
//	I2C_SR3		Status Register 3
//	I2C_ITR		Interrupt Register
//	I2C_CCRL	Clock Control Register Low
//	I2C_CCRH	Clock Control Register High
//	I2C_TRISER	Tristate Enable register
//	funny the I2C_PECR is documented in STM8S103F3 specification while the Packet Error Checking is not part of the S family
//
//all reset to 0
//------------------------------------------------------------------------------------------------------------------

//
// STM8 I2C system.
// default is slave, goes to master on Start and back to slave on stop
// Addresses are 7/10 bits (one or two bytes), a Genral Call address can be enabled or disabled
// 9th bit is acknowledge from the slave
void I2C_Init()
{
	//I2C_Init_Pins();//B5 SDA, B4 SCL
	
	I2C_CR1 = 0;				// clearing (PE) if this is a re - init, this does not stop the ongoing communication
								// CR1_NOSTRETCH	Clock Strtching enabled
								// CR1_ENGC 		General call disabled
								// CR2_POS 			ACK controls the current byte
	I2C_FREQR = 16;             // clk at least 1 MHz for Standard and 4MHz for Fast
	I2C_CCRH_F_S = 0;           // I2C running is standard mode.
	//I2C_CCRL = 0x50;            // I2C period = 2 * CCR * tMASTER 100KHz : tabe 50 RM0016 P 315
	//I2C_CCRH = 0x00;			// CCR[11:8] = 0
	I2C_CCRL = 0xA0;            // I2C period = 2 * CCR * tMASTER 100KHz : tabe 50 RM0016 P 315
	I2C_CCRH = 0x00;			// CCR[11:8] = 0
								// I2C_CCRH_F_S : Standard mode, DUTY unused in standard mode

	I2C_OARH_ADDMODE = 0;               // 7-bit slave address
	I2C_OARH_ADDCONF = 1;               // This bit must be set by software
										// ADD[9:8] unused

	I2C_TRISER = 17;			//Maximum time used by the feedback loop to keep SCL Freq stable whatever SCL rising time is
								//Standard mode max rise time is 1000ns
								//example for 8MHz : (1000ns / 125 ns = 8 ) + 1 = 9
								//for 16 MHz : (1000 ns / 62.5 ns = 16 ) + 1 = 17

	// ------------------------ Interrupts are enabled ------------------------ 
	I2C_ITR_ITEVTEN = 1;                //Event  Enables 				: SB, ADDR, ADD10, STOPF, BTF, WUFH
	I2C_ITR_ITBUFEN = 1;                //Buffer Enables (if ITEVTEN) 	: RXNE, TXE
	I2C_ITR_ITERREN = 1;				//Error  Enables				: BERR, ARLO, AF, OVR

	#if(I2C_Use_Slave == 1)				//As a slave, we start listening, so slave params must be available
	i2c.slaveTxLength = 0;			
        i2c.slaveRxLength = 0;
	#endif
	
	
	
	I2C_CR1_PE = 1;						//Enable the I2C Peripheral
}

#if(I2C_Use_Slave == 1)
void I2C_Slave_Configure(BYTE ownSlaveAddress, 
                         BYTE* rxBuffer, BYTE rxSize,
                         BYTE* txBuffer, BYTE txSize)
{
    I2C_OARL_ADD = ownSlaveAddress;    // 设置从机地址
    i2c.slaveRxBuffer = rxBuffer;      // 接收缓冲区
    i2c.slaveRxLength = rxSize;        // 接收缓冲区大小
    i2c.slaveTxBuffer = txBuffer;      // 发送缓冲区
    i2c.slaveTxLength = txSize;        // 发送缓冲区大小
    i2c.masterMode = 0;                // 切换到从机模式
    
    I2C_CR2_ACK = 1;    // 启用应答
}
#endif

#if(I2C_Use_Master == 1)
void I2C_Transaction(BYTE read,BYTE slaveAddress, BYTE* buffer,BYTE count)
{
	i2c.readwrite = read;
	i2c.SlaveAddress = slaveAddress;
	i2c.buffer_index = 0;
	i2c.masterBuffer = buffer;
	i2c.masterTransactionLength = count;
	i2c.masterMode = 1;
	
	//wait for the Bus to get Free to avoid collisions
	while(I2C_SR3_BUSY);
	
	I2C_CR2_ACK = 1;	//Acknowledge Enable : Acknowledge returned after a byte is received (matched address or data)
	//The start will enter the Master Mode (when the Busy bit is cleared)
	//If already in Master Mode, then ReStart will be generated at the end of the current transfer
	I2C_CR2_START = 1;	//Launch the process
}

void I2C_Read(BYTE slaveAddress, BYTE* buffer,BYTE count)
{
	I2C_Transaction(0x01,slaveAddress,buffer,count);
}

void I2C_Write(BYTE slaveAddress, BYTE* buffer,BYTE count)
{
	I2C_Transaction(0x00,slaveAddress,buffer,count);
}
#endif



//SR1: 	TXE(Tx Empty) RXNE(Rx Not Empty) STOPF(Stop detection, slave mode) 
//		ADD10(10bit header sent, master mode) BTF(Byte Transfer Ffinished) 
//		ADDR(Address sent, master mode / matched in slave mode) SB(Start bit, master mode)

//SR2: 	WUFH(wake up from halt, slave/master) OVR(Overrun underrun) AF(Acknowledge Failure)
//		ARLO(Arbitration lost, master mode) BERR(Bus Error misplaced start or stop)

//SR3: 	DUALF(reserved ?) GENCALL(General call if used) TRA(Transmitted not received as of R/W address bit)
//		BUSY(Bus busy updated even if PE=0) MSL(Master mode set after Start Bit, cleared after Stop or on Arbitration Lost)
#pragma vector = I2C_TXE_vector
//#pragma vector = I2C_RXNE_vector	// all have same vector
__interrupt void I2C_IRQ()
{
    #if (I2C_Use_Slave == 1)
    if (I2C_SR1_ADDR)        // 地址匹配
    {
        i2c.reg = I2C_SR1;   // 清除状态寄存器
        i2c.reg = I2C_SR3;   // 清除状态寄存器
        I2C_CR2_ACK = 1;     // 启用应答
        
        // 根据读写位重置相应的索引
        if (I2C_SR3_TRA) {  // 如果是传输模式（主机要读取）
            i2c.buffer_index = 0;  // 重置发送缓冲区索引
        } else {                    // 如果是接收模式（主机要写入）
            i2c.buffer_index = 0;  // 重置接收缓冲区索引
        }
    }
    else if(I2C_SR1_RXNE)    // 接收数据就绪（主机写入）
    {
        if(i2c.buffer_index < i2c.slaveRxLength) // 接收数据
        {
            i2c.slaveRxBuffer[i2c.buffer_index++] = I2C_DR;
            I2C_CR2_ACK = 1;
            
            //延时一会
            //UARTPrintfLn("I2C_IRQ I2C_SR1_RXNE");
            //delay_1ms();
            delay_100us();
            //delay(160);
            if (i2c.buffer_index == i2c.slaveRxLength)
            {
                i2c_user_Slave_Rx_Callback(i2c.slaveRxBuffer, i2c.slaveRxLength);
            }
        }
        else // 缓冲区已满
        {
            i2c.reg = I2C_DR;    // 丢弃数据
            I2C_CR2_ACK = 1;
        }
    }
    else if (I2C_SR1_TXE)    // 发送寄存器空（主机读取）
    {
        if(i2c.buffer_index < i2c.slaveTxLength) // 发送数据
        {
            I2C_DR = i2c.slaveTxBuffer[i2c.buffer_index++];
            if (i2c.buffer_index == i2c.slaveTxLength)
            {
                i2c_user_Slave_Tx_Callback(i2c.slaveTxBuffer, i2c.slaveTxLength);
            }
        }
        else // 发送缓冲区数据已发完
        {
            I2C_DR = 0xFF;    // 发送默认值或保持最后一次发送
        }
    }
    #endif /*I2C_Use_Slave*/
    
    
    //in either cases, handle the stop notification
    //reading SR1 register followed by a write in the CR2 register	
    
    BYTE error_status = I2C_SR2 & 0x0F;
    if(error_status)				//(OVR)
    {
        if(I2C_SR2_AF)
        {
            I2C_SR2_AF = 0;//This is not an error, it is the end of the slave transmission
        }
        if(I2C_SR2_OVR)
        {
            I2C_SR2_OVR = 0;
        }
        if(I2C_SR2_ARLO)
        {
            I2C_SR2_ARLO = 0;
        }
        if(I2C_SR2_BERR)
        {
            I2C_SR2_BERR = 0;
        }
        if(I2C_SR3_MSL)//If we are still Master of the bus, only then stop the transaction
        {
            I2C_CR2_STOP = 1;	//Generate a Stop condition as a master
        }
        i2c_user_Error_Callback(error_status);
    }
    if(I2C_SR1_STOPF)
    {
        i2c.reg = I2C_SR1;
        I2C_CR2 = 0x00;		//a write to this register is needed to clear the STOP Flag
            
    }
	
}


