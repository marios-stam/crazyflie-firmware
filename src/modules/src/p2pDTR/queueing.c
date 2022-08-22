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

#include "debug.h"

// TX SRV packet queue
static xQueueHandle TX_DATA_queue;

// RX SRV packet queue 
static xQueueHandle RX_SRV_queue;

// RX DATA packet queue
static xQueueHandle RX_DATA_queue;


static xQueueHandle *getQueueHandler(DTRQueue_Names qName){
	switch(qName){
		case TX_DATA_Q:
			return &TX_DATA_queue;
		case RX_SRV_Q:
			return &RX_SRV_queue;
		case RX_DATA_Q:
			return &RX_DATA_queue;
		default:
			// The q enum is not valid
			DEBUG_PRINT("Invalid DTR queue name\n");
			ASSERT(0);
			return NULL;
	}
}


void DTRqueueingInit(){
	// TX SRV queue
	STATIC_MEM_QUEUE_ALLOC(TX_DATA_queue, TX_DATA_QUEUE_SIZE, sizeof(DTRpacket));
	TX_DATA_queue = STATIC_MEM_QUEUE_CREATE(TX_DATA_queue);
	DEBUG_QUEUE_MONITOR_REGISTER(TX_DATA_queue);

	// RX SRV queue
	STATIC_MEM_QUEUE_ALLOC(RX_SRV_queue, RX_SRV_QUEUE_SIZE, sizeof(DTRpacket));
	RX_SRV_queue = STATIC_MEM_QUEUE_CREATE(RX_SRV_queue);
	DEBUG_QUEUE_MONITOR_REGISTER(RX_SRV_queue);

	// RX DATA queue
	STATIC_MEM_QUEUE_ALLOC(RX_DATA_queue, RX_DATA_QUEUE_SIZE, sizeof(DTRpacket));
	RX_DATA_queue = STATIC_MEM_QUEUE_CREATE(RX_DATA_queue);
	DEBUG_QUEUE_MONITOR_REGISTER(RX_DATA_queue);

}

uint8_t DTRgetNumberOfPacketsInQueue(DTRQueue_Names qName){
	return (uint8_t) uxQueueMessagesWaiting(*getQueueHandler(qName));
}

bool DTRisPacketInQueueAvailable(DTRQueue_Names qName) {
	return uxQueueMessagesWaiting(*getQueueHandler(qName)) > 0;
}

bool DTRgetPacketFromQueue(DTRpacket *packet, DTRQueue_Names qName, uint32_t timeout){
	// notice that xQueuePeek is used instead of xQueueReceive, because the packet is not removed from the queue
    //TODO: make a separate function for this
	bool received_success;
	switch (qName)
	{
	case TX_DATA_Q:
		received_success = xQueuePeek(TX_DATA_queue, packet, timeout) == pdTRUE;
		break;
	
	case RX_SRV_Q:
		received_success = xQueueReceive(RX_SRV_queue, packet, timeout) == pdTRUE;
		break;
	case RX_DATA_Q:
		received_success = xQueueReceive(RX_DATA_queue, packet, timeout) == pdTRUE;
		break;

	default:
		DEBUG_PRINT("Invalid DTR queue name\n");
		received_success = false;
		break;
	}
	return received_success;
}

bool DTRreceivePacketWaitUntil(DTRpacket *packet, DTRQueue_Names qName, uint32_t timeout_ms, bool *new_packet_received){
	*new_packet_received = xQueueReceive(*getQueueHandler(qName), packet, M2T(timeout_ms)) == pdTRUE;
	return true;
}

bool DTRinsertPacketToQueue(DTRpacket *packet, DTRQueue_Names qName) {
	bool res = xQueueSend(*getQueueHandler(qName),(void *) packet, 0) == pdTRUE;
	if (!res) {
		DEBUG_PRINT("TX_DATA queue busy\n");
	}

	return res;
}

bool releaseDTRPacketFromQueue(DTRQueue_Names qName) {
	DTRpacket packet;
	return xQueueReceive(*getQueueHandler(qName), &packet, M2T(TX_RECEIVED_WAIT_TIME)) == pdTRUE;
}

void emptyDTRQueue(DTRQueue_Names qName) {
	xQueueReset(*getQueueHandler(qName));
}

void emptyDTRQueues(void){
	emptyDTRQueue(TX_DATA_Q);
	emptyDTRQueue(RX_SRV_Q);
	emptyDTRQueue(RX_DATA_Q);
}

void emptyDTRDataQueues(void){
	emptyDTRQueue(TX_DATA_Q);
	emptyDTRQueue(RX_DATA_Q);
}

