#ifndef __UART_H
#define __UART_H


#define delay_1us(); delay(0);

#define delay_10us(); delay(18);

#define delay_50us(); delay(98);

#define delay_100us(); delay(198);

#define delay_1ms(); delay(1998);

#define delay_10ms(); delay(19998);


void InitialiseUART();

void UARTPrintf(char const *message);

void UARTPrintfLn(char const *message);

void UARTPrintfHex(unsigned char val);

void UARTPrintfHexTable(unsigned char *pval,unsigned char length);

void UARTPrintfHexLn(unsigned char val);

void UARTPrintf_sint(signed int num);

void UARTPrintf_uint(unsigned int num);

void InitialiseSystemClock();

void delay(unsigned int n);

void delay_1ms_Count(unsigned int n);

#endif /* __UART_H */

