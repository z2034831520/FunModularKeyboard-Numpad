#ifndef __ADC_H
#define __ADC_H

#include "iostm8s103f3.h"
#include <stdint.h>
#include <intrinsics.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "common.h"


void ADC_GPIO_Init(void);
void ADC_Init();
void ReadVol_CHx(uint16_t *ain2_val, uint16_t *ain3_val);

#endif




