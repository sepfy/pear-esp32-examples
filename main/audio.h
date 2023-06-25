/*
 * i2s_acc.h
 *
 *  Created on: 2020/09/10
 *      Author: nishi
 */

#ifndef MAIN_I2S_ACC_H_
#define MAIN_I2S_ACC_H_

#include "freertos/FreeRTOS.h"


#include "esp_system.h"
//#define SAMPLE_RATE (16000)
//#define PIN_I2S_BCLK 26
//#define PIN_I2S_LRC 32
//#define PIN_I2S_DIN 33
//#define PIN_I2S_DOUT -1

//      .bck_io_num = 26,    // IIS_SCLK
//      .ws_io_num = 32,     // IIS_LCLK
//      .data_out_num = -1,  // IIS_DSIN
//      .data_in_num = 33,   // IIS_DOUT



void i2s_init(void);

int32_t getSample(uint8_t *dt,int32_t dl);

void audio_task(void *arg);

#endif /* MAIN_I2S_ACC_H_ */
