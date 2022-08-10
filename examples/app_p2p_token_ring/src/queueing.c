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
 * queueing.c
 *
 *  Created on: 14.02.2021
 *      Author: Christos Zosimidis
 *
 *  Modified for the P2P protocol by: Bitcraze AB
 * 
 */


#include "queueing.h"


DTRpacket rxPacketQueue[RX_QUEUE_SIZE];
uint8_t rxQueueWritePos = 0;
uint8_t rxQueueReadPos = 0;

DTRpacket txPacketQueue[TX_QUEUE_SIZE];
uint8_t txQueueWritePos = 0;
uint8_t txQueueReadPos = 0;

bool isRxQueueFull() {
	return ((rxQueueWritePos+1)%RX_QUEUE_SIZE) == rxQueueReadPos;
}
bool isTxQueuePacket() {
	return txQueueReadPos != txQueueWritePos;
}

bool isRadioRxPacketAvailable() {
	return (rxQueueReadPos != rxQueueWritePos);
}

const DTRpacket* readRadioRxPacket() {
	return &rxPacketQueue[rxQueueReadPos];
}

void releaseRadioRxPacket() {
	rxQueueReadPos = (rxQueueReadPos + 1) % RX_QUEUE_SIZE;
}

bool isRadioTxPacketAvailable() {
	if (((txQueueWritePos + 1) % TX_QUEUE_SIZE) != txQueueReadPos) {
		return true;
	}
	// radioMetaInfo.failedTxQueueFull++;
	return false;
}

DTRpacket* getTXWritePacket() {
	return &txPacketQueue[txQueueWritePos];
}

void sendRadioTxPacket() {
	txQueueWritePos = (txQueueWritePos + 1) % TX_QUEUE_SIZE;
}

DTRpacket *getTXReadPacket(){
    return &txPacketQueue[txQueueReadPos];
}

DTRpacket *getRXWritePacket(){
    return &rxPacketQueue[rxQueueWritePos];
}



void incrementTxQueueWritePos() {
    txQueueWritePos = (txQueueWritePos + 1) % TX_QUEUE_SIZE;
}

void incrementTxQueueReadPos() {
    txQueueReadPos = (txQueueReadPos + 1) % TX_QUEUE_SIZE;
}

void incrementRxQueueReadPos() {
    rxQueueReadPos = (rxQueueReadPos + 1) % RX_QUEUE_SIZE;
}

void incrementRxQueueWritePos() {
    rxQueueWritePos = (rxQueueWritePos + 1) % RX_QUEUE_SIZE;
}
