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

// #define DEBUG_MODULE "TOK_RING"
#include "debug.h"


static uint8_t node_id = 0;
static uint8_t next_node_id = 0;
static uint8_t prev_node_id = 0;
static uint8_t next_target_id = 0;
static uint8_t last_packet_source_id = 0;

static uint8_t next_sender_id = 255;

static DTRpacket* timerDTRpacket;

static DTRpacket startPacket; // not sure if this is needed

static DTRpacket servicePk = {
	.dataSize = 0,
	.packetSize = DTR_PACKET_HEADER_SIZE,
	.allToAllFlag = false,
};
static TxStates tx_state, timerRadioTxState;
static RxStates rx_state;


static RadioInfo radioMetaInfo;
static uint32_t protocol_timeout_ms;

static uint8_t my_id ;

static DTRtopology networkTopology;


// DEBUGGING FUNCTIONS
#ifdef DEBUG_DTR_PROTOCOL

// get the message name from the message type
static const char* getMessageType(uint8_t message_type){
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

static const char* getRXState(uint8_t rx_state){
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

static const char* getTXState(uint8_t tx_state){
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

#endif

static uint8_t getIndexInTopology(uint8_t id){
	for(uint8_t i = 0; i < networkTopology.size; i++){
		if(networkTopology.devices_ids[i] == id){
			return i;
		}
	}
	return 255;
} 

static uint8_t getNextNodeId(uint8_t id){
	uint8_t index = getIndexInTopology(id);
	bool index_is_last = index == networkTopology.size - 1;
	uint8_t next_index =  index_is_last ? 0 : index + 1;

	return networkTopology.devices_ids[next_index];
}

static uint8_t getPrevNodeId(uint8_t id){
	uint8_t index = getIndexInTopology(id);

	bool index_is_first = index == 0;
	uint8_t prev_index =  index_is_first ? networkTopology.size - 1 : index - 1;
	return networkTopology.devices_ids[prev_index];
}

static bool IdExistsInTopology(uint8_t id){
	for(uint8_t i = 0; i < networkTopology.size; i++){
		if(networkTopology.devices_ids[i] == id){
			return true;
		}
	}
	return false;
}

static void setNodeIds(DTRtopology topology, uint8_t device_id) {
	networkTopology = topology;
	node_id = device_id;

	servicePk.source_id = node_id;
	last_packet_source_id = node_id;

	next_node_id = getNextNodeId(node_id);
	prev_node_id = getPrevNodeId(node_id);

	DTR_DEBUG_PRINT("self node_id: %d\n", node_id);
	DTR_DEBUG_PRINT("next node_id: %d\n", next_node_id);
	DTR_DEBUG_PRINT("prev node_id: %d\n", prev_node_id);
}

static void initTokenRing(DTRtopology topology, uint8_t device_id) {
	my_id = DTRgetSelfId();

	/* Network node configuration*/
	setNodeIds(topology, my_id);

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
			DTRstartSenderTimer(MAX_WAIT_TIME_FOR_CTS);
			break;

		case TX_TOKEN:
			// set the receiver to RX_WAIT_RTS, after packet is sent
			rx_state = RX_WAIT_RTS;

			// set the radio timer to "spam" token messages
			// setDTRSenderTimer(MAX_WAIT_TIME_FOR_RTS);
			timerDTRpacket = packet;
			DTRstartSenderTimer(MAX_WAIT_TIME_FOR_RTS);
			break;

		case TX_DATA_FRAME:
			// set the receiver to WAIT_DATA_ACK, after packet is sent
			rx_state = RX_WAIT_DATA_ACK;

			// set the radio timer to "spam" the same DATA frame
			// setDTRSenderTimer(MAX_WAIT_TIME_FOR_DATA_ACK);
			timerDTRpacket = packet;
			DTRstartSenderTimer(MAX_WAIT_TIME_FOR_DATA_ACK);
			break;

		default:
			DEBUG_PRINT("\nRadio transmitter state not set correctly!!\n");
			return;
	}
	DTRsendP2Ppacket(packet);
	radioMetaInfo.sendPackets++;


}

static void resetProtocol(void){
	DTR_DEBUG_PRINT("\nResetting protocol\n");
	rx_state = RX_IDLE;
	protocol_timeout_ms = T2M(xTaskGetTickCount()) + PROTOCOL_TIMEOUT_MS;
	emptyDTRQueues(); // Maybe not all queues, but only the RX_SRV since the data are going to be lost saved in DATA queues 
	DTRshutdownSenderTimer();
	last_packet_source_id = 255;
}

static DTRpacket createReconfigurationPacket(void){
	DTRpacket packet;
	packet.message_type = TOPOLOGY_RECONFIG;
	packet.source_id = node_id;
	packet.allToAllFlag = true;	
	packet.dataSize = networkTopology.size + 1;
	packet.packetSize = packet.dataSize + DTR_PACKET_HEADER_SIZE;

	packet.data[0] = networkTopology.size;  // 1st byte is the number of nodes in the topology
	for (uint8_t i = 0; i < networkTopology.size; i++) {
		packet.data[i+1] = networkTopology.devices_ids[i];
	}

	return packet;
}

static void printTopology(void){
	DEBUG_PRINT("TOPOLOGY: ");
	for (size_t i = 0; i < networkTopology.size; i++)
	{
		DEBUG_PRINT("%d ", networkTopology.devices_ids[i]);
	}
	DEBUG_PRINT("\n");
}

static void reconfigureTopology(uint8_t node_id_to_remove){
	uint8_t index_to_remove = getIndexInTopology(node_id_to_remove);
	DEBUG_PRINT("Index to remove: %d\n", index_to_remove);
	if(index_to_remove == 255){
		DTR_DEBUG_PRINT("\nNode to remove not found in topology\n");
		return;
	}

	uint8_t distEnd = networkTopology.size - index_to_remove - 1;
	uint8_t distStart = index_to_remove;
	for(uint8_t i = 0; i < distEnd; i++){
		networkTopology.devices_ids[distStart] = networkTopology.devices_ids[distStart + 1];
		distStart++;
	}

	networkTopology.size = networkTopology.size -1;

	printTopology();
	

	setNodeIds(networkTopology, node_id);

}

static void reconfigureTopologyFromPacket(DTRpacket *packet){
	// 1st byte is the number of nodes in the topology
	uint8_t new_size = packet->data[0]; 
	networkTopology.size = new_size;
	for(uint8_t i = 0; i < new_size; i++){
		networkTopology.devices_ids[i] = packet->data[i+1];
	}
	printTopology();
	// set next and prev node ids after reconfiguration
	setNodeIds(networkTopology, node_id);
}

static void sendTestData(){
	DTRpacket  testSignal;
	testSignal.message_type = DATA_FRAME;
	testSignal.source_id = my_id;
	testSignal.target_id = 1;
	uint8_t data_size = 25;
	for (int i = 0; i < data_size; i++){
		testSignal.data[i] = i;
	}
	testSignal.dataSize = data_size;
	testSignal.allToAllFlag = 1;
	testSignal.packetSize = DTR_PACKET_HEADER_SIZE + testSignal.dataSize;
	bool res;
	for (int i = 0; i < TX_DATA_QUEUE_SIZE - 1; i++){
		testSignal.data[0] = 100+i;
		res = DTRsendPacket(&testSignal);
		if (res){
			DTR_DEBUG_PRINT("Packet sent to DTR protocol\n");
		}
		else{
			DEBUG_PRINT("Packet not sent to DTR protocol\n");
		}
	}
}

static uint8_t getNonRespondingNodeId(void){
	uint8_t not_responding_node_id;
	if (rx_state == RX_WAIT_CTS){
			not_responding_node_id = prev_node_id;
	} else if (rx_state == RX_WAIT_DATA_ACK || rx_state == RX_WAIT_RTS){
		not_responding_node_id = (next_sender_id == 255) ? next_node_id : next_sender_id;			
	}else{
		DEBUG_PRINT("\nTimeout in wrong state\n");
		not_responding_node_id = 255;
	}

	return not_responding_node_id;
}

static DTRpacket TopologyReconfigPacket;
static void DTRhandleTimeout(void){
	DEBUG_PRINT("\nTimeout while being in rx state: %d \n", rx_state);
	DTRshutdownSenderTimer();
	bool have_token = (rx_state == RX_WAIT_CTS) || (rx_state == RX_WAIT_DATA_ACK) || (rx_state == RX_WAIT_RTS);
	if (have_token){
		DEBUG_PRINT ("I have the token so I will reconfigure the topology...\n");
		// One node of the network is not responding, consider the network as broken
		// and reconfigure the network topology
		
		if(networkTopology.size > 2){
			// find the node that is not responding
			uint8_t not_responding_node_id = getNonRespondingNodeId();
			if (not_responding_node_id == 255){
				DEBUG_PRINT("\nNode not responding not found\n");
				return;
			}
			
			DEBUG_PRINT("not responding node id: %d\n", not_responding_node_id);

			// reconfigure the topology
			reconfigureTopology(not_responding_node_id);
			
			//create a special packet to inform the other nodes of the reconfiguration
			TopologyReconfigPacket = createReconfigurationPacket();
			
			DEBUG_PRINT("Printing topology reconfiguration packet...\n");
			for (int i = 0; i < TopologyReconfigPacket.dataSize; i++){
				DEBUG_PRINT("%d ", TopologyReconfigPacket.data[i]);
			}
			DEBUG_PRINT("\n");



			setupRadioTx(&TopologyReconfigPacket, TX_DATA_FRAME);

			//send test data to the new topology
			emptyDTRDataQueues();
			sendTestData();

		}else{
			// only one node in the network, setting into idle state
			DEBUG_PRINT("\n Only one drone left in network..\n");
			resetProtocol();
		}

	}else{
		// rx state is RX_IDLE
		resetProtocol();
	}
}

void DTRInterruptHandler(void *param) {

	DTRpacket* rxPk, *txPk;

	//TODO: bad implementation, should be fixed
	DTRpacket _rxPk ;
	protocol_timeout_ms = T2M(xTaskGetTickCount()) + PROTOCOL_TIMEOUT_MS;
	bool new_packet_received;

	DTR_DEBUG_PRINT("\nDTRInterruptHandler Task called...\n");
	while ( DTRreceivePacketWaitUntil(&_rxPk, 	RX_SRV_Q, PROTOCOL_TIMEOUT_MS, &new_packet_received) ){
			if (!new_packet_received) {
				DTR_DEBUG_PRINT("\nPROTOCOL TIMEOUT!\n");
				DTRhandleTimeout();	
				continue;
			}

			rxPk = &_rxPk;	
			
			radioMetaInfo.receivedPackets++;

			DTR_DEBUG_PRINT("===============================================================\n");
			DTR_DEBUG_PRINT("=\n");
			DTR_DEBUG_PRINT("TX_DATA Q Empty: %d\n", !DTRisPacketInQueueAvailable(TX_DATA_Q));


			DTR_DEBUG_PRINT("rx_state: %s\n", getRXState(rx_state));
			
			// DTRprintPacket(rxPk);
			DTR_DEBUG_PRINT("Received packet type: %d\n", rxPk->message_type);
			DTR_DEBUG_PRINT("Received packet target id: %d\n", rxPk->target_id);
			DTR_DEBUG_PRINT("my_id: %d\n", node_id);

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
							bool queueFull = DTRinsertPacketToQueue(rxPk, RX_DATA_Q);
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
					
					/* if packet is TOPOLOGY_RECONFIG packet then reconfigure the  topology 
					* based on the data included */
					if (rxPk->message_type == TOPOLOGY_RECONFIG) {
						DEBUG_PRINT("\nReceived TOPOLOGY_RECONFIG\n");
						reconfigureTopologyFromPacket(rxPk);
						//print new topology

						DTRpacket  testSignal;
						testSignal.message_type = DATA_FRAME;
						testSignal.source_id = my_id;
						testSignal.target_id = 1;
						
						uint8_t data_size = 25;
						for (int i = 0; i < data_size; i++){
							testSignal.data[i] = i;
						}
						testSignal.data[0] = 132;
						 
						testSignal.dataSize = data_size;
						testSignal.allToAllFlag = 1;
						testSignal.packetSize = DTR_PACKET_HEADER_SIZE + testSignal.dataSize;
						
						bool res = DTRsendPacket(&testSignal);
						if (!res){
							DEBUG_PRINT("\nERROR: Failed to send packet\n");
						}
						/* Acknowledge the data */
						DEBUG_PRINT("\nSending ACK TOP RECONFIG packet\n");
						servicePk.message_type = DATA_ACK_FRAME;
						servicePk.target_id = rxPk->source_id;
						setupRadioTx(&servicePk, TX_DATA_ACK);
						continue;
					}

					DTR_DEBUG_PRINT("\nRECEIVED PACKET NOT HANDLED\n");
					/* drop all other packets and restart receiver */
					break;

				case RX_WAIT_CTS:

					/* if packet is CTS and received from previous node, then node can
					 * send its next DATA packet. */
					if (rxPk->message_type == CTS_FRAME && rxPk->source_id == prev_node_id) {
						DTRshutdownSenderTimer();
						last_packet_source_id = node_id;
						DTR_DEBUG_PRINT("\nRcvd CTS from prev,send DATA to next\n");
						/* check if there is a DATA packet. If yes, prepare it and
						 * send it, otherwise forward the token to the next node. */
						
						DTRpacket _txPk;//TODO: bad implementation, should be fixed
						if (DTRgetPacketFromQueue(&_txPk, TX_DATA_Q, M2T(TX_RECEIVED_WAIT_TIME))) {
							txPk = &_txPk;
							DTR_DEBUG_PRINT("TX DATA Packet exists (%d), sending it\n",txPk->data[0]);
							if(txPk->allToAllFlag) {
								txPk->target_id = next_node_id;
							}
							if (!IdExistsInTopology(txPk->target_id) && !txPk->allToAllFlag) {
								DEBUG_PRINT("Releasing DTR TX packet,target is not in topology.\n");
								DTR_DEBUG_PRINT("Is Queue Empty: %d\n", !DTRisPacketInQueueAvailable(TX_DATA_Q));
								
								releaseDTRPacketFromQueue(TX_DATA_Q);
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
							DTR_DEBUG_PRINT("No TX DATA,forwarding token to next\n");
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
						DTRshutdownSenderTimer();
						servicePk.message_type = CTS_FRAME;
						setupRadioTx(&servicePk, TX_CTS);
						continue;
					}

					/* drop all other packets and restart receiver */
					break;

				case RX_WAIT_DATA_ACK:
					if (rxPk->message_type == DATA_ACK_FRAME && rxPk->target_id == node_id) {
						DTRshutdownSenderTimer();

						//TODO: bad implementation, should be fixed
						DTRpacket _txPk;
						DTRgetPacketFromQueue(&_txPk, TX_DATA_Q, M2T(TX_RECEIVED_WAIT_TIME));
						txPk = &_txPk;
						
						if (next_sender_id == 255){
							next_target_id = getNextNodeId(node_id);
						}
						else {
							next_target_id = getNextNodeId(next_sender_id);
						}



						DTR_DEBUG_PRINT("next_target_id: %d\n", next_target_id);
						if (!txPk->allToAllFlag || next_target_id == node_id) {
							DTR_DEBUG_PRINT("Releasing TX DATA:\n");
							// DTRprintPacket(txPk);

							DTR_DEBUG_PRINT("Is Q Empty: %d\n", !DTRisPacketInQueueAvailable(TX_DATA_Q));

							releaseDTRPacketFromQueue(TX_DATA_Q);
							txPk = &servicePk;
							txPk->message_type = TOKEN_FRAME;
							tx_state = TX_TOKEN;
							next_sender_id = 255;
						} else {								
							txPk->target_id = next_target_id;
							next_sender_id = next_target_id;
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

			// TODO: There are cases that the packet received from queue
			// is the same to the one that was previously received and already processed.
			// Maybe when releasing the packet, also release the ones that are similar to it in the queue.

			// DEBUG_PRINT("Packet Received cannot be processed\n");
			// DEBUG_PRINT("State: %d\n", rx_state);
			// DEBUG_PRINT("Packet type: %d\n", rxPk->message_type);

			// resetProtocol(); //TODO: test if it makes sense
		}
	
}

uint8_t DTRgetDeviceAddress() {
	return node_id;
}

void DTRtimeOutCallBack(xTimerHandle timer) {
	#ifdef DEBUG_DTR_PROTOCOL
	DTR_DEBUG_PRINT("Sending packet after timeout: %s",getMessageType(timerDTRpacket->message_type));
	if (timerDTRpacket->message_type == DATA_FRAME) {
		for (size_t i = 0; i <timerDTRpacket->dataSize; i++)
		{
			DTR_DEBUG_PRINT(" %d ",timerDTRpacket->data[i]);
		}
	}
	DTR_DEBUG_PRINT("\n");
	#endif

	DTRsendP2Ppacket(timerDTRpacket);
	radioMetaInfo.sendPackets++;

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

const RadioInfo* DTRgetRadioInfo() {
	radioMetaInfo.deviceId = node_id;
	return &radioMetaInfo;
}

void DTRresetRadioMetaInfo() {
	memset(&radioMetaInfo, 0, sizeof(radioMetaInfo));
}

void DTRstartCommunication() {
	DTRpacket* startSignal = &startPacket;

	startSignal->source_id = node_id;
	startSignal->target_id = next_node_id;
	startSignal->data[0] = 111;
	startSignal->dataSize = 1;
	startSignal->packetSize = DTR_PACKET_HEADER_SIZE + startSignal->dataSize;
	startSignal->message_type = DATA_FRAME; 
	timerDTRpacket = startSignal;

	timerRadioTxState = TX_DATA_FRAME;
	rx_state = RX_WAIT_DATA_ACK;
	DTR_DEBUG_PRINT("Spamming DATA\n");
	DTRstartSenderTimer(MAX_WAIT_TIME_FOR_DATA_ACK);
}


void DTRprintPacket(DTRpacket* packet){
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

uint8_t DTRgetSelfId(void){
	// Get the current address of the crazyflie and 
	//keep the last 2 digits as the id of the crazyflie.
	uint64_t address = configblockGetRadioAddress();
	uint8_t my_id = (uint8_t)((address)&0x00000000ff);
	return my_id;
}

void DTRenableProtocol(DTRtopology topology){
	DTR_DEBUG_PRINT("Initializing queues ...\n");
	DTRqueueingInit();

	DTR_DEBUG_PRINT("Initializing token ring ...\n");
	initTokenRing(topology, my_id);
	
	DTR_DEBUG_PRINT("Initializing DTR Sender ...\n");
	DTRinitSenderTimer();

	DTR_DEBUG_PRINT("Starting protocol timer ...\n");
	DTRstartProtocolTask();

	// The first node starts the protocol communication 
	if (my_id == topology.devices_ids[0]) {
		DTRstartCommunication();
	}
}

void DisableDTRProtocol(void){
	DTR_DEBUG_PRINT("Stopping protocol timer ...\n");
	DTRstopProtocolTask();
	
	DTR_DEBUG_PRINT("Stopping DTR Sender ...\n");
	DTRshutdownSenderTimer();
	
	DTR_DEBUG_PRINT("Resetting token ring ...\n");
	resetProtocol();
	
	DTR_DEBUG_PRINT("Stopping queues ...\n");
	emptyDTRQueues();
}


bool DTRsendPacket(DTRpacket* packet){
	packet->message_type = DATA_FRAME;
	packet->source_id = node_id;
	packet->packetSize = DTR_PACKET_HEADER_SIZE + packet->dataSize;
	return  DTRinsertPacketToQueue(packet,TX_DATA_Q);
}


bool DTRgetPacket(DTRpacket* packet, uint32_t timeout){
	if (timeout != portMAX_DELAY){
		timeout = M2T(timeout);
	} 

	return  DTRgetPacketFromQueue(packet,RX_DATA_Q,timeout);
}


LOG_GROUP_START(DTR_P2P)
	LOG_ADD(LOG_UINT8, rx_state, &rx_state)
	LOG_ADD(LOG_UINT8, tx_state, &tx_state)
LOG_GROUP_STOP(DTR_P2P)
