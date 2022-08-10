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
 * token_ring.c
 *
 *  Created on: 28.01.2021
 *      Author: Christos Zosimidis
 *	
 *  Modified for the P2P protocol by: Bitcraze AB
 */


#include "token_ring.h"

#define DEBUG_MODULE "TOK_RING"
#include "debug.h"

#define MAX_WAIT_TIME_FOR_RTS 2500 // 2.5ms
#define MAX_WAIT_TIME_FOR_CTS 2500 // 2.5ms
#define MAX_WAIT_TIME_FOR_DATA_ACK 2500 // 2.5ms

static uint8_t MAX_NETWORK_SIZE = 0;
static uint8_t node_id = 0;
static uint8_t next_node_id = 0;
static uint8_t prev_node_id = 0;
static uint8_t next_target_id = 0;
static uint8_t last_packet_source_id = 0;

static DTRpacket* timerDTRpacket;
static DTRpacket servicePk = {
	.dataSize = 0,
	.packetSize = DTR_PACKET_HEADER_SIZE,
	.allToAllFlag = false,
};
static RadioModes radioMode;
static TxStates tx_state, timerRadioTxState;
static RxStates rx_state;


static RadioInfo radioMetaInfo;

//declaring
static void rumpUpRadioInRx();


static void setNodeIds(uint8_t networkSize, uint8_t device_id) {
	MAX_NETWORK_SIZE = networkSize;
	node_id = device_id;
	servicePk.source_id = node_id;
	last_packet_source_id = node_id;
	next_node_id = (node_id + 1) % MAX_NETWORK_SIZE;
	prev_node_id = (node_id + (MAX_NETWORK_SIZE - 1)) % MAX_NETWORK_SIZE;
}

void initTokenRing(uint8_t networkSize, uint8_t device_id) {

	/* Network node configuration*/
	setNodeIds(networkSize, device_id);

	/* Interrupt Configuration */
	// NVIC_SetPriority(RADIO_IRQn, 2);
	// NVIC_EnableIRQ(RADIO_IRQn);


	radioMode = RX_MODE;
	rx_state = RX_IDLE;
}

static void setupRadioTx(DTRpacket* packet, TxStates txState) {

	tx_state = txState;
	switch (tx_state) {

		case TX_DATA_ACK:
			// set the receiver to IDLE, after packet is sent
			rx_state = RX_IDLE;
			break;

		case TX_CTS:
			// set the receiver to IDLE, after packet is sent
			rx_state = RX_IDLE;
			break;

		case TX_RTS:
			// set the receiver to WAIT_FOR_CTS, after packet is sent
			rx_state = RX_WAIT_CTS;

			// set the radio timer to "spam" RTS messages
			setRadioTimer(MAX_WAIT_TIME_FOR_CTS);
			timerDTRpacket = packet;
			startRadioTimer();
			break;

		case TX_TOKEN:
			// set the receiver to RX_WAIT_RTS, after packet is sent
			rx_state = RX_WAIT_RTS;

			// set the radio timer to "spam" token messages
			setRadioTimer(MAX_WAIT_TIME_FOR_RTS);
			timerDTRpacket = packet;
			startRadioTimer();
			break;

		case TX_DATA_FRAME:
			// set the receiver to WAIT_DATA_ACK, after packet is sent
			rx_state = RX_WAIT_DATA_ACK;

			// set the radio timer to "spam" the same DATA frame
			setRadioTimer(MAX_WAIT_TIME_FOR_DATA_ACK);
			timerDTRpacket = packet;
			startRadioTimer();
			break;

		default:
			DEBUG_PRINT("\nRadio transmitter state not set correctly!!\n");
			return;
	}
	sendDTRpacket(packet);
}


void DTRInterruptHandler(void) {

	DTRpacket* rxPk, *txPk;

	/* There are two cases in which this interrupt routine can be called:
	 * 1. when the radio was receiving
	 * 2. when the radio was transmitting
	 * Each case should be handled accordingly */
	switch (radioMode) {

		case RX_MODE:
			radioMetaInfo.receivedPackets++;
			rxPk = getRXWritePacket();

			/* Receiver radio states of DTR-Protocol */
			switch (rx_state) {

				case RX_IDLE:

					/* if packet is DATA packet and received from previous node,
					* then it can be handled. */
					if (rxPk->message_type == DATA_FRAME && rxPk->target_id == node_id) {

						if (rxPk->source_id != last_packet_source_id) {
							last_packet_source_id = rxPk->source_id;
							/* if packet is relevant and receiver queue is not full, then
							* push packet in the queue and prepare queue for next packet. */
							if (!isRxQueueFull()) {
								incrementRxQueueWritePos();
							} else {
								radioMetaInfo.failedRxQueueFull++;
							}
						}
							/* Acknowledge the data */
							servicePk.message_type = DATA_ACK_FRAME;
							servicePk.target_id = rxPk->source_id;
							setupRadioTx(&servicePk, TX_DATA_ACK);
							return;
						}

					/* if packet is TOKEN packet and received from previous node, then
					* send a TOKEN_ACK packet and prepare the packet for the timer. */
					if (rxPk->message_type == TOKEN_FRAME && rxPk->source_id == prev_node_id) {
						servicePk.message_type = RTS_FRAME;
						setupRadioTx(&servicePk, TX_RTS);
						return;
					}

					/* if packet is RTS packet and received from next node, then send
					* a CTS packet to next node. */
					if (rxPk->message_type == RTS_FRAME && rxPk->source_id == next_node_id) {
						servicePk.message_type = CTS_FRAME;
						setupRadioTx(&servicePk, TX_CTS);
						return;
					}

					/* drop all other packets and restart receiver */
					break;

				case RX_WAIT_CTS:

					/* if packet is CTS and received from previous node, then node can
					 * send its next DATA packet. */
					if (rxPk->message_type == CTS_FRAME && rxPk->source_id == prev_node_id) {
						shutdownRadioTimer();
						last_packet_source_id = node_id;
						/* check if there is a DATA packet. If yes, prepare it and
						 * send it, otherwise forward the token to the next node. */

						if (isTxQueuePacket()) {
							txPk = getTXReadPacket();
							if(txPk->allToAllFlag) {
								txPk->target_id = next_node_id;
							}
							if (txPk->target_id >= RADIO_DEFAULT_NETWORK_SIZE) {
								incrementTxQueueReadPos();
								txPk = &servicePk;
								txPk->message_type = TOKEN_FRAME;
								tx_state = TX_TOKEN;
							} else {
								txPk->packetSize = DTR_PACKET_HEADER_SIZE + txPk->dataSize;
								txPk->source_id = node_id;
								txPk->message_type = DATA_FRAME;
								tx_state = TX_DATA_FRAME;
							}
						} else {
							txPk = &servicePk;
							txPk->message_type = TOKEN_FRAME;
							tx_state = TX_TOKEN;
						}

						setupRadioTx(txPk, tx_state);
						return;
					}

					/* drop all other packets and restart receiver */
					break;

				case RX_WAIT_RTS:

					/* if packet is TOKEN_ACK and received from next node, then
					 * a CTS packet can be sent. */
					if (rxPk->message_type == RTS_FRAME && rxPk->source_id == next_node_id) {
						shutdownRadioTimer();
						// NVIC_ClearPendingIRQ(TIMER0_IRQn);
						// NVIC_ClearPendingIRQ(RADIO_IRQn);
						servicePk.message_type = CTS_FRAME;
						setupRadioTx(&servicePk, TX_CTS);
						return;
					}

					/* drop all other packets and restart receiver */
					break;

				case RX_WAIT_DATA_ACK:

					if (rxPk->message_type == DATA_ACK_FRAME && rxPk->target_id == node_id) {
						shutdownRadioTimer();
						// NVIC_ClearPendingIRQ(TIMER0_IRQn);
						// NVIC_ClearPendingIRQ(RADIO_IRQn);

						txPk = getTXReadPacket();
						next_target_id = (txPk->target_id + 1) % MAX_NETWORK_SIZE;

						if (!txPk->allToAllFlag || next_target_id == node_id) {
							incrementTxQueueReadPos();
							txPk = &servicePk;
							txPk->message_type = TOKEN_FRAME;
							tx_state = TX_TOKEN;
						} else {
							txPk->target_id = next_target_id;
							tx_state = TX_DATA_FRAME;
						}
						setupRadioTx(txPk, tx_state);
						return;
					}

					/* drop all other packets and restart receiver */
					break;

				default:
					DEBUG_PRINT("\nRadio receiver state not set correctly!!\n");
					return;
				}

			/* restart receiver */
			// NRF_RADIO->TASKS_START = 1UL;
			break;

		case TX_MODE:

			rumpUpRadioInRx();
			radioMetaInfo.sendPackets++;
			break;

		default:
			DEBUG_PRINT("\nRadio mode not set correctly!!\n");
			return;
		}
	
}

static void rumpUpRadioInRx() {
	// Reads the latest radio packet received and enters RX mode.
	// TODO: Maybe add a queue to store the received packets to be processed. 
	// CHRISTOS: What is happening exactly here?

	*getRXWritePacket() = *getLatestDTRpacket(); //TODO: check if this is correct
	
	radioMode = RX_MODE;
}

void setDeviceRadioAddress(uint8_t radio_address) {
	node_id = radio_address;
}

uint8_t getDeviceRadioAddress() {
	return node_id;
}

void timeOutCallBack() {
	sendDTRpacket(timerDTRpacket);
	switch(tx_state) {
		case TX_RTS:
			radioMetaInfo.timeOutRTS++;
			break;
		case TX_TOKEN:
			radioMetaInfo.timeOutTOKEN++;
			break;
		case TX_DATA_FRAME:
			radioMetaInfo.timeOutDATA++;
			break;
		default:
			break;
	}
}

const RadioInfo* getRadioInfo() {
	radioMetaInfo.deviceId = node_id;
	return &radioMetaInfo;
}

void resetRadioMetaInfo() {
	memset(&radioMetaInfo, 0, sizeof(radioMetaInfo));
}

void startRadioCommunication() {

	DTRpacket* startSignal = getTXReadPacket();

	startSignal->source_id = node_id;
	startSignal->target_id = next_node_id;
	startSignal->packetSize = DTR_PACKET_HEADER_SIZE;

	timerDTRpacket = startSignal;
	setRadioTimer(MAX_WAIT_TIME_FOR_DATA_ACK);
	incrementTxQueueWritePos();
	timerRadioTxState = TX_DATA_FRAME;
	radioMode = RX_MODE;
	rx_state = RX_WAIT_DATA_ACK;
	startRadioTimer();
}

