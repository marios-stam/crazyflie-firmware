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
 * queueing.h
 *
 *  Created on: 14.02.2021
 *      Author: Christos Zosimidis
 *
 *  Modified for the P2P protocol by: Bitcraze AB
 * 
 */

#ifndef _QUEUEING_H_
#define _QUEUEING_H_

#include "FreeRTOS.h"
#include "DTR_types.h"

#include "token_ring.h"

#define RX_QUEUE_SIZE 20
#define TX_QUEUE_SIZE 20

bool isRxQueueFull();

bool isTxQueuePacket();

bool isRadioRxPacketAvailable();

void releaseRadioRxPacket();

bool isRadioTxPacketAvailable();

void sendRadioTxPacket();

// write the DATA packet that needs to be sent to other nodes(from user)
DTRpacket* getTXWritePacket();

// read DATA to be send to others
DTRpacket* getTXReadPacket();

// read the latest packet received from the radio
DTRpacket* getRXWritePacket();

// read the DATA packet received from other node (from user)
const DTRpacket* readRadioRxPacket();

// Append index space for the next DATA packet that user needs to send
void incrementTxQueueWritePos();

// read DATA from the next index in the queue
void incrementTxQueueReadPos();

// keep the last packet in the queue for the user to read it.
void incrementRxQueueReadPos();

// release the DATA packet from the queue (means that it has been read by the user)
void incrementRxQueueWritePos();

#endif /* _QUEUEING_H_ */
