/*
 * MIT License
 *
 * Copyright (c) 2022 Christos Zosimidis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *
 * token_ring.h
 *
 *  Created on: 28.01.2021
 *      Author: Christos Zosimidis
 *
 *  Modified for the P2P protocol by: Bitcraze AB
 */

#ifndef SRC_RADIO_RADIO_H_
#define SRC_RADIO_RADIO_H_

#include "stdint.h"
#include "stdbool.h"
#include "string.h"
#include "debug.h"
#include "radiolink.h"

#include "DTR_types.h"
#include "DTR_timers.h"
#include "p2p_interface.h"
#include "queueing.h"

#define RADIO_DEFAULT_NETWORK_SIZE 2
#define RADIO_BROADCAST_ADDRESS 0xFF
#define RADIO_NRF51_BOARD_ADDRESS 0x00

#define RADIO_DEFAULT_DEVICE_ADDRESS 0x01

void initRadio(uint8_t networkSize, uint8_t device_id);

void initTokenRing(uint8_t networkSize, uint8_t device_id);

void setDeviceRadioAddress (uint8_t radio_address);

uint8_t getDeviceRadioAddress();

void timeOutCallBack();

void startRadioCommunication();

void DTRInterruptHandler(void);

const RadioInfo* getRadioInfo();

void resetRadioMetaInfo();

#endif /* SRC_RADIO_RADIO_H_ */
