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

// TX SRV packet queue
static xQueueHandle TX_DATA_queue;
STATIC_MEM_QUEUE_ALLOC(TX_DATA_queue, TX_DATA_QUEUE_SIZE, sizeof(DTRpacket));

// RX SRV packet queue 
static xQueueHandle RX_SRV_queue;
STATIC_MEM_QUEUE_ALLOC(RX_SRV_queue, RX_SRV_QUEUE_SIZE, sizeof(DTRpacket));

// RX DATA packet queue
static xQueueHandle RX_DATA_queue;
STATIC_MEM_QUEUE_ALLOC(RX_DATA_queue, RX_DATA_QUEUE_SIZE, sizeof(DTRpacket));



void queueing_init(){
	// TX SRV queue
	TX_DATA_queue = STATIC_MEM_QUEUE_CREATE(TX_DATA_queue);
	DEBUG_QUEUE_MONITOR_REGISTER(TX_DATA_queue);

	// RX SRV queue
	RX_SRV_queue = STATIC_MEM_QUEUE_CREATE(RX_SRV_queue);
	DEBUG_QUEUE_MONITOR_REGISTER(RX_SRV_queue);

	// RX DATA queue
	RX_DATA_queue = STATIC_MEM_QUEUE_CREATE(RX_DATA_queue);
	DEBUG_QUEUE_MONITOR_REGISTER(RX_DATA_queue);

}

// ======================== checks ===========================
bool isTX_DATAPacketAvailable(void) {
	return uxQueueMessagesWaiting(TX_DATA_queue) > 0;
}

bool isRX_SRVPacketAvailable(void) {
	return uxQueueMessagesWaiting(RX_SRV_queue) > 0;
}

bool isRX_DATAPacketAvailable(void) {
	return uxQueueMessagesWaiting(RX_DATA_queue) > 0;
}

// ======================== getters ========================  

// read DATA to be send to others
bool getTX_DATA_packet(DTRpacket *packet) {
	// notice that xQueuePeek is used instead of xQueueReceive, because the packet is not removed from the queue
    //TODO: make a separate function for this
	bool received_success = xQueuePeek(TX_DATA_queue, packet, M2T( TX_RECEIVED_WAIT_TIME )) == pdTRUE;
	return received_success;
}

// read the DATA packet received from other node (from user)
bool  getRX_DATA_packet(DTRpacket *packet) {
	bool received_success = xQueueReceive(RX_DATA_queue, packet, portMAX_DELAY) == pdTRUE;
	if (!received_success) {
		DEBUG_PRINT("RX_DATA queue empty\n");
	}

	return received_success;
}

// read the latest SRV packet received from the radio
bool getRX_SRV_packet(DTRpacket *packet){
	bool received_success = xQueueReceive(RX_SRV_queue, packet, M2T(RX_RECEIVED_WAIT_TIME)) == pdTRUE;
	if (!received_success) {
		DEBUG_PRINT("Not able to receive RX_SRV packet from Q\n");
	}

    return received_success;
}

bool receiveRX_SRV_packet_wait_until(DTRpacket* packet) {
	return xQueueReceive(RX_SRV_queue, packet, portMAX_DELAY);
}

// ======================== senders ========================

bool sendTX_DATA_packet(DTRpacket *packet) {
	bool res = xQueueSend(TX_DATA_queue,(void *) packet, 0) == pdTRUE;
	if (!res) {
		DEBUG_PRINT("TX_DATA queue busy\n");
	}

	return res;
}

bool sendRX_DATA_packet(DTRpacket *packet) {
	bool res = xQueueSend(RX_DATA_queue,(void *) packet, 0) == pdTRUE;
	if (!res) {
		DEBUG_PRINT("RX_DATA queue busy\n");
	}

	return res;
}

bool sendRX_SRV_packet(DTRpacket *packet) {
	bool res = xQueueSend(RX_SRV_queue,(void *) packet, 0) == pdTRUE;
	if (!res) {
		DEBUG_PRINT("Not able to insert RX_SRV packet in Q \n");
	}
	return res;
}


// ======================= releasers =======================
bool releaseTX_DATA_packet() {
	// DEBUG_PRINT("Releasing TX DATA Packet...\n");

	DTRpacket packet;
	return xQueueReceive(TX_DATA_queue, &packet, M2T(TX_RECEIVED_WAIT_TIME)) == pdTRUE;
}

// release the DATA packet from the queue (means that it has been read by the user)
bool releaseRX_DATA_packet() {
	DTRpacket packet;
	return xQueueReceive(RX_DATA_queue, &packet, M2T(RX_RECEIVED_WAIT_TIME)) == pdTRUE;
}

// release the SRV packet from the queue (means that it has been read by the user)
bool releaseRX_SRV_packet() {
	DTRpacket packet;
	return xQueueReceive(RX_SRV_queue, &packet, M2T(RX_RECEIVED_WAIT_TIME)) == pdTRUE;
}


void emptyTX_DATA_queue(void){
	xQueueReset(TX_DATA_queue);
}

void emptyRX_DATA_queue(void){
	xQueueReset(RX_DATA_queue);
}

void emptyRX_SRV_queue(void){
	xQueueReset(RX_SRV_queue);
}

void emptyQueues(void){
	emptyTX_DATA_queue();
	emptyRX_DATA_queue();
	emptyRX_SRV_queue();
}
