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


static uint8_t MAX_NETWORK_SIZE = 0;
static uint8_t node_id = 0;
static uint8_t next_node_id = 0;
static uint8_t prev_node_id = 0;
static uint8_t next_target_id = 0;
static uint8_t last_packet_source_id = 0;

static DTRpacket* timerDTRpacket;

static DTRpacket startPacket; // not sure if this is needed

static DTRpacket servicePk = {
	.dataSize = 0,
	.packetSize = DTR_PACKET_HEADER_SIZE,
	.allToAllFlag = false,
};
static RadioModes radioMode;
static TxStates tx_state, timerRadioTxState;
static RxStates rx_state;


static RadioInfo radioMetaInfo;
static uint32_t protocol_timeout_ms;

static uint8_t my_id ;

static void setNodeIds(uint8_t networkSize, uint8_t device_id) {
	MAX_NETWORK_SIZE = networkSize;
	node_id = device_id;
	servicePk.source_id = node_id;
	last_packet_source_id = node_id;
	next_node_id = (node_id + 1) % MAX_NETWORK_SIZE;
	prev_node_id = (node_id + (MAX_NETWORK_SIZE - 1)) % MAX_NETWORK_SIZE;
}

void initTokenRing(uint8_t networkSize, uint8_t device_id) {
	my_id = get_self_id();

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
	DTR_DEBUG_PRINT("Setting up radio tx with state: %s \n", getTXState(tx_state));

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
			// setDTRSenderTimer(MAX_WAIT_TIME_FOR_CTS);
			timerDTRpacket = packet;
			startDTRSenderTimer(MAX_WAIT_TIME_FOR_CTS);
			break;

		case TX_TOKEN:
			// set the receiver to RX_WAIT_RTS, after packet is sent
			rx_state = RX_WAIT_RTS;

			// set the radio timer to "spam" token messages
			// setDTRSenderTimer(MAX_WAIT_TIME_FOR_RTS);
			timerDTRpacket = packet;
			startDTRSenderTimer(MAX_WAIT_TIME_FOR_RTS);
			break;

		case TX_DATA_FRAME:
			// set the receiver to WAIT_DATA_ACK, after packet is sent
			rx_state = RX_WAIT_DATA_ACK;

			// set the radio timer to "spam" the same DATA frame
			// setDTRSenderTimer(MAX_WAIT_TIME_FOR_DATA_ACK);
			timerDTRpacket = packet;
			startDTRSenderTimer(MAX_WAIT_TIME_FOR_DATA_ACK);
			break;

		default:
			DEBUG_PRINT("\nRadio transmitter state not set correctly!!\n");
			return;
	}
	sendDTRpacket(packet);
	// radioMode = TX_MODE;
	radioMetaInfo.sendPackets++;


}

static void resetProtocol(void){
	DTR_DEBUG_PRINT("\nResetting protocol\n");
	rx_state = RX_IDLE;
	protocol_timeout_ms = T2M(xTaskGetTickCount()) + PROTOCOL_TIMEOUT_MS;
	emptyQueues();
	shutdownDTRSenderTimer();
	last_packet_source_id = 255;
}

static uint8_t send_data_to_peer_counter = 0;

void DTRInterruptHandler(void *param) {

	DTRpacket* rxPk, *txPk;

	//TODO: bad implementation, should be fixed
	DTRpacket _rxPk ;
	protocol_timeout_ms = T2M(xTaskGetTickCount()) + PROTOCOL_TIMEOUT_MS;
	bool new_packet_received;

	while ( receiveRX_SRV_packet_wait_until(&_rxPk, PROTOCOL_TIMEOUT_MS,&new_packet_received) ){
			if (!new_packet_received) {
				DTR_DEBUG_PRINT("\nPROTOCOL TIMEOUT!\n");
				if (my_id != 0){
					resetProtocol();
				}
				
				continue;
			}

			rxPk = &_rxPk;	
			
			radioMetaInfo.receivedPackets++;

			DTR_DEBUG_PRINT("===============================================================\n");
			DTR_DEBUG_PRINT("=\n");
			DTR_DEBUG_PRINT("TX_DATA Q Empty: %d\n", !isTX_DATAPacketAvailable());


			DTR_DEBUG_PRINT("rx_state: %s\n", getRXState(rx_state));
			
			// printDTRPacket(rxPk);

			switch (rx_state) {

				case RX_IDLE:
					/* if packet is DATA packet and received from previous node,
					* then it can be handled. */
					if (rxPk->message_type == DATA_FRAME && rxPk->target_id == node_id) {
						DTR_DEBUG_PRINT("\nReceived DATA packet from prev\n");
						if (rxPk->source_id != last_packet_source_id) {
							last_packet_source_id = rxPk->source_id;
							/* if packet is relevant and receiver queue is not full, then
							* push packet in the queue and prepare queue for next packet. */
							bool queueFull = sendRX_DATA_packet(rxPk);
							if (queueFull){
								radioMetaInfo.failedRxQueueFull++;
							}
						}
							/* Acknowledge the data */
							DTR_DEBUG_PRINT("Received DATA %d\n", rxPk->data[0]);
							DTR_DEBUG_PRINT("\nSending ACK packet\n");
							servicePk.message_type = DATA_ACK_FRAME;
							servicePk.target_id = rxPk->source_id;
							setupRadioTx(&servicePk, TX_DATA_ACK);
							continue;
						}

					/* if packet is TOKEN packet and received from previous node, then
					* send a TOKEN_ACK packet and prepare the packet for the timer. */
					if (rxPk->message_type == TOKEN_FRAME && rxPk->source_id == prev_node_id) {
						DTR_DEBUG_PRINT("\nReceived TOKEN from prev\n");
						servicePk.message_type = RTS_FRAME;
						setupRadioTx(&servicePk, TX_RTS);
						continue;
					}

					/* if packet is RTS packet and received from next node, then send
					* a CTS packet to next node. */
					if (rxPk->message_type == RTS_FRAME && rxPk->source_id == next_node_id) {
						DTR_DEBUG_PRINT("\nReceived RTS from next -> CTS to next\n");
						servicePk.message_type = CTS_FRAME;
						setupRadioTx(&servicePk, TX_CTS);
						continue;
					}
					
					DTR_DEBUG_PRINT("\nRECEIVED PACKET NOT HANDLED\n");
					/* drop all other packets and restart receiver */
					break;

				case RX_WAIT_CTS:

					/* if packet is CTS and received from previous node, then node can
					 * send its next DATA packet. */
					if (rxPk->message_type == CTS_FRAME && rxPk->source_id == prev_node_id) {
						shutdownDTRSenderTimer();
						last_packet_source_id = node_id;
						DTR_DEBUG_PRINT("\nRcvd CTS from prev,send DATA to next\n");
						/* check if there is a DATA packet. If yes, prepare it and
						 * send it, otherwise forward the token to the next node. */
						
						DTRpacket _txPk;//TODO: bad implementation, should be fixed
						if (getTX_DATA_packet(&_txPk)) {
							txPk = &_txPk;
							DTR_DEBUG_PRINT("TX DATA Packet exists (%d), sending it\n",txPk->data[0]);
							if(txPk->allToAllFlag) {
								txPk->target_id = next_node_id;
							}
							if (txPk->target_id >= MAX_NETWORK_SIZE) {
								DTR_DEBUG_PRINT("Releasing TX DATA Packet because target > default:\n");

								DTR_DEBUG_PRINT("Is Queue Empty: %d\n", !isTX_DATAPacketAvailable());
								
								releaseTX_DATA_packet();
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
							DTR_DEBUG_PRINT("No TX DATA,fwding token to next\n");
							txPk = &servicePk;
							txPk->message_type = TOKEN_FRAME;
							tx_state = TX_TOKEN;
						}

						setupRadioTx(txPk, tx_state);
						continue;
					}

					/* drop all other packets and restart receiver */
					break;

				case RX_WAIT_RTS:

					/* if packet is TOKEN_ACK and received from next node, then
					 * a CTS packet can be sent. */
					if (rxPk->message_type == RTS_FRAME && rxPk->source_id == next_node_id) {
						DTR_DEBUG_PRINT("\nReceived TOKEN_ACK from next->sending CTS \n");
						shutdownDTRSenderTimer();
						servicePk.message_type = CTS_FRAME;
						setupRadioTx(&servicePk, TX_CTS);
						continue;
					}

					/* drop all other packets and restart receiver */
					break;

				case RX_WAIT_DATA_ACK:
					if (rxPk->message_type == DATA_ACK_FRAME && rxPk->target_id == node_id) {
						shutdownDTRSenderTimer();

						//TODO: bad implementation, should be fixed
						DTRpacket _txPk;
						getTX_DATA_packet(&_txPk);
						txPk = &_txPk;
						
						next_target_id = (txPk->target_id + send_data_to_peer_counter + 1) % MAX_NETWORK_SIZE;
						DTR_DEBUG_PRINT("next_target_id: %d\n", next_target_id);

						if (!txPk->allToAllFlag || next_target_id == node_id) {
							DTR_DEBUG_PRINT("Releasing TX DATA:\n");
							// printDTRPacket(txPk);

							DTR_DEBUG_PRINT("Is Q Empty: %d\n", !isTX_DATAPacketAvailable());

							releaseTX_DATA_packet();
							txPk = &servicePk;
							txPk->message_type = TOKEN_FRAME;
							tx_state = TX_TOKEN;
							send_data_to_peer_counter = 0;
						} else {								
							txPk->target_id = next_target_id;
							send_data_to_peer_counter++;
							DTR_DEBUG_PRINT("Sending DATA to next peer..");
							DTR_DEBUG_PRINT("with target id: %d\n", txPk->target_id);
							tx_state = TX_DATA_FRAME;
						}
						setupRadioTx(txPk, tx_state);
						continue;
					}

					/* drop all other packets and restart receiver */
					break;

				default:
					DEBUG_PRINT("\nRadio receiver state not set correctly!!\n");
					continue;
				}

			// DEBUG_PRINT("Packet Received cannot be processed\n");
			// resetProtocol(); //TODO: test if it makes sense
		}
	
}

void setDeviceRadioAddress(uint8_t radio_address) {
	node_id = radio_address;
}

uint8_t getDeviceRadioAddress() {
	return node_id;
}

void timeOutCallBack(xTimerHandle timer) {
	#ifdef DEBUG_DTR
	DTR_DEBUG_PRINT("Sending packet after timeout: %s",getMessageType(timerDTRpacket->message_type));
	if (timerDTRpacket->message_type == DATA_FRAME) {
		for (size_t i = 0; i <timerDTRpacket->dataSize; i++)
		{
			DTR_DEBUG_PRINT(" %d ",timerDTRpacket->data[i]);
		}
	}
	DTR_DEBUG_PRINT("\n");
	#endif

	sendDTRpacket(timerDTRpacket);
	radioMetaInfo.sendPackets++;

	// radioMode = TX_MODE;

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
	DTRpacket* startSignal = &startPacket;

	startSignal->source_id = node_id;
	startSignal->target_id = next_node_id;
	startSignal->data[0] = 69;
	startSignal->dataSize = 1;
	startSignal->packetSize = DTR_PACKET_HEADER_SIZE + startSignal->dataSize;

	timerDTRpacket = startSignal;
	// setDTRSenderTimer(MAX_WAIT_TIME_FOR_DATA_ACK);
	// incrementTxQueueWritePos();
	timerRadioTxState = TX_DATA_FRAME;
	radioMode = RX_MODE;
	rx_state = RX_WAIT_DATA_ACK;
	DTR_DEBUG_PRINT("Spamming DATA\n");
	startDTRSenderTimer(MAX_WAIT_TIME_FOR_DATA_ACK);
}

// get the message name from the message type
const char* getMessageType(uint8_t message_type){
	switch(message_type){
	case DATA_FRAME:
		return "DATA";
	case TOKEN_FRAME:
		return "TOKEN";
	case CTS_FRAME:
		return "CTS";
	case RTS_FRAME:
		return "RTS";
	case DATA_ACK_FRAME:
		return "DATA_ACK";
	default:
		return "UNKNOWN";
	}
}

const char* getRXState(uint8_t rx_state){
	switch(rx_state){
	case RX_IDLE:
		return "RX_IDLE";
	case RX_WAIT_CTS:
		return "RX_WAIT_CTS";
	case RX_WAIT_RTS:
		return "RX_WAIT_RTS";
	case RX_WAIT_DATA_ACK:
		return "RX_WAIT_DATA_ACK";
	default:
		return "UNKNOWN";
	}
}

const char* getTXState(uint8_t tx_state){
	switch(tx_state){
		case TX_TOKEN:
			return "TX_TOKEN";
		case TX_RTS:
			return "TX_RTS";
		case TX_CTS:
			return "TX_CTS";
		case TX_DATA_FRAME:
			return "TX_DATA_FRAME";
		case TX_DATA_ACK:
			return "TX_DATA_ACK";
	default:
		return "UNKNOWN";
	}
} 

void printDTRPacket(DTRpacket* packet){
	DTR_DEBUG_PRINT("\nDTR Packet Received: %s\n", getMessageType(packet->message_type));
	DTR_DEBUG_PRINT("Packet Size: %d\n", packet->packetSize);
	DTR_DEBUG_PRINT("Message Type: %s\n", getMessageType(packet->message_type));
	DTR_DEBUG_PRINT("Source ID: %d\n", packet->source_id);
	DTR_DEBUG_PRINT("Target ID: %d\n", packet->target_id);
	DTR_DEBUG_PRINT("AllToAll Flag: %d\n", packet->allToAllFlag);
	if (packet->dataSize > 0) {
		DEBUG_PRINT("Data: ");
		for (int i = 0; i < packet->dataSize; i++) {
			DEBUG_PRINT("%d ", packet->data[i]);
		}
		DEBUG_PRINT("\n");
	}

	DTR_DEBUG_PRINT("\n\n");
}

uint8_t get_self_id(void){
	// Get the current address of the crazyflie and 
	//keep the last 2 digits as the id of the crazyflie.
	uint64_t address = configblockGetRadioAddress();
	uint8_t my_id = (uint8_t)((address)&0x00000000ff);
	return my_id;
}

LOG_GROUP_START(DTR_P2P)
	LOG_ADD(LOG_UINT8, rx_state, &rx_state)
	LOG_ADD(LOG_UINT8, tx_state, &tx_state)
LOG_GROUP_STOP(DTR_P2P)
