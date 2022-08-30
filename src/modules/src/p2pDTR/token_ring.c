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

static void sendTestData();
static void setupRadioTx(dtrPacket *packet, dtrTxStates txState);

static uint8_t node_id = 0;
static uint8_t next_node_id = 0;
static uint8_t prev_node_id = 0;
static uint8_t next_target_id = 0;
static uint8_t last_packet_source_id = 0;

static dtrPacket *timerDTRpacket;

#ifdef STATIC_PREDEFINED_TOPOLOGY
static dtrPacket startPacket; // not sure if this is needed
static dtrTxStates timerRadioTxState;
#endif

static dtrTxStates tx_state;
static dtrPacket servicePk = {
	.dataSize = 0,
	.packetSize = DTR_PACKET_HEADER_SIZE,
	.targetId = 254,
};

static dtrRxStates rx_state;

static dtrRadioInfo radioMetaInfo;
static uint32_t protocol_timeout_ms;

static uint8_t my_id;
static uint8_t next_sender_id = 255;

static dtrTopology networkTopology;

// Handshake
static dtrTopology handshakeTopology = {
	.size = 255,
	.devices_ids = {0},
};
static uint32_t handshakeTimeoutMs;
static uint32_t now_ms;
static dtrPacket handshakePacket;

// Topology Reconfiguration
static dtrPacket TopologyReconfigPacket;
static uint8_t nodeWithToken = 255;
static bool hasBeenTimedOut = false;

// DEBUGGING FUNCTIONS
#ifdef DEBUG_DTR_PROTOCOL

// get the message name from the message type
static const char *getMessageType(uint8_t messageType)
{
	switch (messageType)
	{
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

static const char *getRXState(uint8_t rx_state)
{
	switch (rx_state)
	{
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

static const char *getTXState(uint8_t tx_state)
{
	switch (tx_state)
	{
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
// ===================== TOPOLOGY FUNCTIONS =====================
static uint8_t getIndexInTopology(uint8_t id)
{
	for (uint8_t i = 0; i < networkTopology.size; i++)
	{
		if (networkTopology.devices_ids[i] == id)
		{
			return i;
		}
	}
	return 255;
}

static uint8_t getNextNodeId(uint8_t id)
{
	uint8_t index = getIndexInTopology(id);
	bool index_is_last = index == networkTopology.size - 1;
	uint8_t next_index = index_is_last ? 0 : index + 1;

	return networkTopology.devices_ids[next_index];
}

static uint8_t getPrevNodeId(uint8_t id)
{
	uint8_t index = getIndexInTopology(id);

	bool index_is_first = index == 0;
	uint8_t prev_index = index_is_first ? networkTopology.size - 1 : index - 1;
	return networkTopology.devices_ids[prev_index];
}

static bool IdExistsInTopology(uint8_t id)
{
	for (uint8_t i = 0; i < networkTopology.size; i++)
	{
		if (networkTopology.devices_ids[i] == id)
		{
			return true;
		}
	}
	return false;
}

static void setNodeIds(dtrTopology topology, uint8_t device_id)
{
	networkTopology = topology;
	node_id = device_id;

	servicePk.sourceId = node_id;
	last_packet_source_id = node_id;

	next_node_id = getNextNodeId(node_id);
	prev_node_id = getPrevNodeId(node_id);

	DTR_DEBUG_PRINT("self node_id: %d\n", node_id);
	DTR_DEBUG_PRINT("next node_id: %d\n", next_node_id);
	DTR_DEBUG_PRINT("prev node_id: %d\n", prev_node_id);
}

static dtrPacket createReconfigurationPacket(dtrTopology topology)
{
	dtrPacket packet;
	packet.messageType = TOPOLOGY_RECONFIG;
	packet.sourceId = node_id;
	packet.targetId = BROADCAST_ID;
	packet.dataSize = networkTopology.size + 1;
	packet.packetSize = packet.dataSize + DTR_PACKET_HEADER_SIZE;

	packet.data[0] = topology.size; // 1st byte is the number of nodes in the topology
	for (uint8_t i = 0; i < topology.size; i++)
	{
		packet.data[i + 1] = topology.devices_ids[i];
	}

	return packet;
}

static void printTopology(void)
{
	DEBUG_PRINT("TOPOLOGY: ");
	for (size_t i = 0; i < networkTopology.size; i++)
	{
		DEBUG_PRINT("%d ", networkTopology.devices_ids[i]);
	}
	DEBUG_PRINT("\n");
}

static void reconfigureTopology(uint8_t node_id_to_remove)
{
	uint8_t index_to_remove = getIndexInTopology(node_id_to_remove);
	DEBUG_PRINT("Index to remove: %d\n", index_to_remove);
	if (index_to_remove == 255)
	{
		DTR_DEBUG_PRINT("\nNode to remove not found in topology\n");
		return;
	}

	uint8_t distEnd = networkTopology.size - index_to_remove - 1;
	uint8_t distStart = index_to_remove;
	for (uint8_t i = 0; i < distEnd; i++)
	{
		networkTopology.devices_ids[distStart] = networkTopology.devices_ids[distStart + 1];
		distStart++;
	}

	networkTopology.size = networkTopology.size - 1;

	printTopology();

	setNodeIds(networkTopology, node_id);
}

static void reconfigureTopologyFromPacket(dtrPacket *packet)
{
	// 1st byte is the number of nodes in the topology
	uint8_t new_size = packet->data[0];
	networkTopology.size = new_size;
	for (uint8_t i = 0; i < new_size; i++)
	{
		networkTopology.devices_ids[i] = packet->data[i + 1];
	}
	printTopology();
	// set next and prev node ids after reconfiguration
	setNodeIds(networkTopology, node_id);
}

static uint8_t getNonRespondingNodeId(void)
{
	uint8_t not_responding_node_id;
	if (rx_state == RX_WAIT_CTS)
	{
		not_responding_node_id = prev_node_id;
	}
	else if (rx_state == RX_WAIT_DATA_ACK || rx_state == RX_WAIT_RTS)
	{
		not_responding_node_id = (next_sender_id == 255) ? next_node_id : next_sender_id;
	}
	else
	{
		DEBUG_PRINT("\nTimeout in wrong state\n");
		not_responding_node_id = 255;
	}

	return not_responding_node_id;
}

static void updateNodeWithToken(dtrPacket *packet)
{
	bool token_transaction = false;
	DTR_DEBUG_PRINT("=====================================\n");
	switch (packet->messageType)
	{
	case TOKEN_FRAME:
		// a node requested a token change
		DTR_DEBUG_PRINT("TOKEN FRAME from %d\n", packet->sourceId);
		token_transaction = true;
		nodeWithToken = getNextNodeId(packet->sourceId);
		break;
	case RTS_FRAME:
		// received a token acknowledgement from a node
		DTR_DEBUG_PRINT("RTS FRAME from %d\n", packet->sourceId);
		token_transaction = true;
		nodeWithToken = packet->sourceId;
		break;
	default:
		// do nothing
		break;
	}

	if (token_transaction)
	{
		DTR_DEBUG_PRINT("Node with token: %d\n", nodeWithToken);
	}
}

static void topologyReconfigSequence(uint8_t not_responding_node_id)
{
	// reconfigure the topology
	if (not_responding_node_id != 255)
	{
		reconfigureTopology(not_responding_node_id);
	}

	// create a special packet to inform the other nodes of the reconfiguration
	TopologyReconfigPacket = createReconfigurationPacket(networkTopology);

	DEBUG_PRINT("Printing topology reconfiguration packet...\n");
	for (int i = 0; i < TopologyReconfigPacket.dataSize; i++)
	{
		DEBUG_PRINT("%d ", TopologyReconfigPacket.data[i]);
	}
	DEBUG_PRINT("\n");

	setupRadioTx(&TopologyReconfigPacket, TX_DATA_FRAME);

	// send test data to the new topology
	dtrEmptyDataQueues();
	sendTestData();
}
// ==============================================================

// ===================== HANDSHAKE HANDLERS =====================
static void initHandshakePacket(void)
{
	handshakePacket.messageType = HANDSHAKE_FRAME;
	handshakePacket.sourceId = dtrGetSelfId();

	// add self to the topology
	handshakePacket.dataSize = 1;
}

void dtrHandshakeTimeOutCallBack(xTimerHandle timer)
{
	uint8_t top_size = handshakeTopology.size;

	memcpy(&handshakePacket.data, &handshakeTopology.devices_ids, top_size);

	handshakePacket.dataSize = top_size;
	handshakePacket.messageType = HANDSHAKE_FRAME;
	handshakePacket.packetSize = DTR_PACKET_HEADER_SIZE + handshakePacket.dataSize;
	dtrSendP2Ppacket(&handshakePacket);
}

static bool isNodeKnownInHandshakeTopology(uint8_t id)
{
	for (uint8_t i = 0; i < handshakeTopology.size; i++)
	{
		if (handshakeTopology.devices_ids[i] == id)
		{
			return true;
		}
	}
	return false;
}

static void addNodeToHandshakeTopology(uint8_t id)
{
	if (handshakeTopology.size < MAX_NETWORK_SIZE || handshakeTopology.size == 255)
	{
		if (handshakeTopology.size == 255)
		{
			handshakeTopology.size = 1;
		}
		else
		{
			handshakeTopology.size++;
		}

		handshakeTopology.devices_ids[handshakeTopology.size-1] = id;
	}
	else
	{
		DEBUG_PRINT("Handshake topology is full:%d\n", handshakeTopology.size);
	}
}

void dtrHandleHandshakePacket(dtrPacket *packet)
{
	// DEBUG_PRINT("Handling handshake packet from %d\n", packet->sourceId);
	// DEBUG_PRINT("PACKET data size: %d\n", packet->dataSize);
	for (int i = 0; i < packet->dataSize; i++)
	{
		uint8_t id = packet->data[i];
		// DEBUG_PRINT("Checking:%d...\n", id);
		if (!isNodeKnownInHandshakeTopology(id))
		{	
			addNodeToHandshakeTopology(id);
			DEBUG_PRINT("Node %d added to handshake topology\n", id);

			handshakeTimeoutMs = now_ms + HANDSHAKE_TIMEOUT_MS;
		}else{
			// DEBUG_PRINT("Node %d already in handshake topology\n", id);
		}
	}
}

static uint8_t minimumIdOfTopology(dtrTopology topology)
{
	uint8_t min = topology.devices_ids[0];
	for (int i = 1; i < topology.size; i++)
	{
		if (topology.devices_ids[i] < min)
		{
			min = topology.devices_ids[i];
		}
	}
	return min;
}
// ==============================================================

static void initTokenRing(dtrTopology topology, uint8_t device_id)
{
	my_id = dtrGetSelfId();
	DEBUG_PRINT("My id: %d\n", my_id);
/* Network node configuration*/
#ifdef STATIC_PREDEFINED_TOPOLOGY
	setNodeIds(topology, my_id);
	rx_state = RX_IDLE;

#else
	DEBUG_PRINT("Initializing HANDSHAKE STUFF...\n");
	initHandshakePacket();
	// add self to the handshake topology
	addNodeToHandshakeTopology(my_id);

	rx_state = RX_HANDSHAKE;
	node_id = dtrGetSelfId();
#endif
}

static void setupRadioTx(dtrPacket *packet, dtrTxStates txState)
{

	tx_state = txState;
	DTR_DEBUG_PRINT("Setting up radio tx with state: %s \n", getTXState(tx_state));

	switch (tx_state)
	{

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
		timerDTRpacket = packet;
		dtrStartSenderTimer(MAX_WAIT_TIME_FOR_CTS);
		break;

	case TX_TOKEN:
		// set the receiver to RX_WAIT_RTS, after packet is sent
		rx_state = RX_WAIT_RTS;

		// set the radio timer to "spam" token messages
		// setDTRSenderTimer(MAX_WAIT_TIME_FOR_RTS);
		timerDTRpacket = packet;
		dtrStartSenderTimer(MAX_WAIT_TIME_FOR_RTS);
		break;

	case TX_DATA_FRAME:
		// set the receiver to WAIT_DATA_ACK, after packet is sent
		rx_state = RX_WAIT_DATA_ACK;

		// set the radio timer to "spam" the same DATA frame
		// setDTRSenderTimer(MAX_WAIT_TIME_FOR_DATA_ACK);
		timerDTRpacket = packet;
		dtrStartSenderTimer(MAX_WAIT_TIME_FOR_DATA_ACK);
		break;

	default:
		DEBUG_PRINT("\nRadio transmitter state not set correctly!!\n");
		return;
	}
	dtrSendP2Ppacket(packet);
	radioMetaInfo.sendPackets++;
}

static void resetProtocol(void)
{
	DTR_DEBUG_PRINT("\nResetting protocol\n");
	rx_state = RX_IDLE;
	protocol_timeout_ms = T2M(xTaskGetTickCount()) + PROTOCOL_TIMEOUT_MS;
	dtrEmptyQueues(); // Maybe not all queues, but only the RX_SRV since the data are going to be lost saved in DATA queues
	dtrShutdownSenderTimer();
	last_packet_source_id = 255;
}

static void sendTestData()
{
	dtrPacket testSignal;
	testSignal.messageType = DATA_FRAME;
	testSignal.sourceId = my_id;

	const char test_data[] = "Sending test data...";
	uint8_t data_size = strlen(test_data);
	testSignal.dataSize = data_size;
	for (int i = 0; i < data_size; i++)
	{
		testSignal.data[i] = test_data[i];
	}

	DEBUG_PRINT("\nSending test data...\n");

	testSignal.targetId = BROADCAST_ID;
	testSignal.packetSize = DTR_PACKET_HEADER_SIZE + testSignal.dataSize;

	bool res;
	res = dtrSendPacket(&testSignal);
	if (res)
	{
		DTR_DEBUG_PRINT("Packet sent to DTR protocol\n");
	}
	else
	{
		DEBUG_PRINT("Packet not sent to DTR protocol\n");
	}
}

static void dtrHandleLostNode(void)
{
	DTR_DEBUG_PRINT("\nTimeout while being in rx state: %d \n", rx_state);
	dtrShutdownSenderTimer();
	bool have_token = (rx_state == RX_WAIT_CTS) || (rx_state == RX_WAIT_DATA_ACK) || (rx_state == RX_WAIT_RTS);
	if (have_token)
	{
		DEBUG_PRINT("I have the token so I will reconfigure the topology...\n");
		// One node of the network is not responding, consider the network as broken
		// and reconfigure the network topology

		if (networkTopology.size > 2)
		{
			// find the node that is not responding
			uint8_t not_responding_node_id = getNonRespondingNodeId();
			if (not_responding_node_id == 255)
			{
				DEBUG_PRINT("\nNode not responding not found\n");
				return;
			}

			DEBUG_PRINT("not responding node id: %d\n", not_responding_node_id);

			topologyReconfigSequence(not_responding_node_id);
		}
		else
		{
			// only one node in the network, setting into idle state
			DEBUG_PRINT("\n Only one drone left in network..\n");
			resetProtocol();
		}
	}
	else
	{
		// rx state is RX_IDLE
		if (!hasBeenTimedOut)
		{
			// if it is the first time the timeout is triggered, reset the protocol
			resetProtocol();
			hasBeenTimedOut = true;
		}
		else if (node_id == getPrevNodeId(nodeWithToken))
		{
			/* If it is the second time the timeout is triggered, it means that the node that dies had the token
			 * If this node is the one before the node with the token that died
			 * claim the token and start reconfiguration
			 */
			DEBUG_PRINT("\nI am the one before the node with the token that died\n");
			topologyReconfigSequence(nodeWithToken);
			hasBeenTimedOut = false;
		}
		else
		{
			DEBUG_PRINT("\nI am not the one before the node with the token that died and no one has claimed the node yet\n");
			DEBUG_PRINT("Node with token: %d\n", nodeWithToken);
			printTopology();

			DEBUG_PRINT("Node with token prev: %d\n", getPrevNodeId(nodeWithToken));
			DEBUG_PRINT("Node with token next: %d\n", getNextNodeId(nodeWithToken));

			DEBUG_PRINT("Aggresive token claim...\n");
			if (networkTopology.devices_ids[0] == node_id)
			{
				// I am the first node in the network, I claim the token
				DEBUG_PRINT("I am the first node in the network, I claim the token\n");
				topologyReconfigSequence(255); // 255 means that no node will be removed but the token will be claimed
			}
		}
	}
}

// returns true if loop iteration should be skipped
static bool dtrHandleReceiveTimeout(bool new_packet_received)
{

	DTR_DEBUG_PRINT("\nPROTOCOL TIMEOUT!\n");
	if (now_ms < protocol_timeout_ms)
	{
		// do nothing
	}
	else if (radioMetaInfo.receivedPackets > 0)
	{
		// A node is not responding, but there are packets received, so it is lost.
		dtrHandleLostNode();
		return true;
	}

	return false;
}

// ================================ MAIN TASK =================================
void dtrTaskHandler(void *param)
{

	dtrPacket *rxPk, *txPk;

	// TODO: bad implementation, should be fixed
	dtrPacket _rxPk;
	protocol_timeout_ms = T2M(xTaskGetTickCount()) + PROTOCOL_TIMEOUT_MS;
	bool new_packet_received;

	rx_state = RX_HANDSHAKE;

	now_ms = T2M(xTaskGetTickCount());
	handshakeTimeoutMs = now_ms + HANDSHAKE_TIMEOUT_MS;
	DEBUG_PRINT("\nDTRInterruptHandler Task called...\n");
	while (dtrReceivePacketWaitUntil(&_rxPk, RX_SRV_Q, PROTOCOL_TIMEOUT_MS, &new_packet_received))
	{	

		if (!new_packet_received)
		{
			if (dtrHandleReceiveTimeout(new_packet_received))
			{
				continue;
			}
		}

		// Don't count handshake packets
		if (!(rxPk->messageType == HANDSHAKE_FRAME))
		{
			radioMetaInfo.receivedPackets++;
		}

		rxPk = &_rxPk;

		updateNodeWithToken(rxPk);
		now_ms = T2M(xTaskGetTickCount());

		switch (rx_state)
		{
		case RX_HANDSHAKE:
		{
			bool handshake_timeout = (now_ms > handshakeTimeoutMs);
			if (!handshake_timeout && rxPk->messageType == HANDSHAKE_FRAME)
			{
				dtrHandleHandshakePacket(rxPk);
			}
			else if (handshake_timeout)
			{
				dtrShutdownHandshakeTimer();
				// check if this is the node with the smaller id
				if (my_id <= minimumIdOfTopology(handshakeTopology))
				{
					DEBUG_PRINT("\nI am the node with the smallest id, I claim the token\n");
					memcpy(&networkTopology.devices_ids, &handshakeTopology.devices_ids, handshakeTopology.size);
					networkTopology.size = handshakeTopology.size;

					// Configure Topology without removing any node
					topologyReconfigSequence(255);
				}
				else
				{
					DEBUG_PRINT("Handshake timeout, going to RX_IDLE...\n");
					rx_state = RX_IDLE;
				}
			}
		}
		break;
		case RX_IDLE:
			/* if packet is DATA packet and received from previous node,
			 * then it can be handled. */
			if (rxPk->messageType == DATA_FRAME && rxPk->targetId == node_id)
			{
				DTR_DEBUG_PRINT("\nReceived DATA packet from prev\n");
				bool received_starting_packet = (rxPk->data[0] == (uint8_t)(START_PACKET >> 8)) && (rxPk->data[1] == (uint8_t)START_PACKET);
				if (rxPk->sourceId != last_packet_source_id && !received_starting_packet)
				{
					last_packet_source_id = rxPk->sourceId;
					/* if packet is relevant and receiver queue is not full, then
					 * push packet in the queue and prepare queue for next packet. */
					bool queueFull = !dtrInsertPacketToQueue(rxPk, RX_DATA_Q);
					if (queueFull)
					{
						radioMetaInfo.failedRxQueueFull++;
					}
				}

				/* Acknowledge the data */
				DTR_DEBUG_PRINT("Received DATA %d\n", rxPk->data[0]);
				DTR_DEBUG_PRINT("\nSending ACK packet\n");
				servicePk.messageType = DATA_ACK_FRAME;
				servicePk.targetId = rxPk->sourceId;
				setupRadioTx(&servicePk, TX_DATA_ACK);
				continue;
			}

			/* if packet is TOKEN packet and received from previous node, then
			 * send a TOKEN_ACK packet and prepare the packet for the timer. */
			if (rxPk->messageType == TOKEN_FRAME && rxPk->sourceId == prev_node_id)
			{
				DTR_DEBUG_PRINT("\nReceived TOKEN from prev\n");
				servicePk.messageType = RTS_FRAME;
				setupRadioTx(&servicePk, TX_RTS);
				continue;
			}

			/* if packet is RTS packet and received from next node, then send
			 * a CTS packet to next node. */
			if (rxPk->messageType == RTS_FRAME && rxPk->sourceId == next_node_id)
			{
				DTR_DEBUG_PRINT("\nReceived RTS from next -> CTS to next\n");
				servicePk.messageType = CTS_FRAME;
				setupRadioTx(&servicePk, TX_CTS);
				continue;
			}

			/* if packet is TOPOLOGY_RECONFIG packet then reconfigure the  topology
			 * based on the data included */
			if (rxPk->messageType == TOPOLOGY_RECONFIG)
			{
				DEBUG_PRINT("\nReceived TOPOLOGY_RECONFIG from: %d\n", rxPk->sourceId);
				reconfigureTopologyFromPacket(rxPk);
				hasBeenTimedOut = false;
				// print new topology

				dtrPacket testSignal;
				testSignal.messageType = DATA_FRAME;
				testSignal.sourceId = my_id;
				testSignal.targetId = 1;

				const char responseToReconf[] = "Response to reconfig";
				uint8_t data_size = strlen(responseToReconf);
				testSignal.dataSize = data_size;
				for (int i = 0; i < data_size; i++)
				{
					testSignal.data[i] = responseToReconf[i];
				}

				testSignal.targetId = BROADCAST_ID;
				testSignal.packetSize = DTR_PACKET_HEADER_SIZE + testSignal.dataSize;

				bool res = dtrSendPacket(&testSignal);
				if (!res)
				{
					DEBUG_PRINT("\nERROR: Failed to send packet\n");
				}
				/* Acknowledge the data */
				DEBUG_PRINT("\nSending ACK TOP RECONFIG packet\n");
				servicePk.messageType = DATA_ACK_FRAME;
				servicePk.targetId = rxPk->sourceId;
				setupRadioTx(&servicePk, TX_DATA_ACK);
				continue;
			}

			DTR_DEBUG_PRINT("\nRECEIVED PACKET NOT HANDLED\n");
			/* drop all other packets and restart receiver */
			break;

		case RX_WAIT_CTS:

			/* if packet is CTS and received from previous node, then node can
			 * send its next DATA packet. */
			if (rxPk->messageType == CTS_FRAME && rxPk->sourceId == prev_node_id)
			{
				dtrShutdownSenderTimer();
				last_packet_source_id = node_id;
				DTR_DEBUG_PRINT("\nRcvd CTS from prev,send DATA to next\n");
				/* check if there is a DATA packet. If yes, prepare it and
				 * send it, otherwise forward the token to the next node. */

				dtrPacket _txPk; // TODO: bad implementation, should be fixed
				if (dtrGetPacketFromQueue(&_txPk, TX_DATA_Q, M2T(TX_RECEIVED_WAIT_TIME)))
				{
					txPk = &_txPk;
					DTR_DEBUG_PRINT("TX DATA Packet exists (dataSize: %d), sending it\n", txPk->dataSize);
					if (txPk->targetId == BROADCAST_ID)
					{
						txPk->targetId = next_node_id;
					}
					if (!IdExistsInTopology(txPk->targetId))
					{
						DEBUG_PRINT("Releasing DTR TX packet,target is not in topology.\n");
						DTR_DEBUG_PRINT("Is Queue Empty: %d\n", !dtrIsPacketInQueueAvailable(TX_DATA_Q));

						dtrReleasePacketFromQueue(TX_DATA_Q);
						txPk = &servicePk;
						txPk->messageType = TOKEN_FRAME;
						tx_state = TX_TOKEN;
					}
					else
					{
						txPk->packetSize = DTR_PACKET_HEADER_SIZE + txPk->dataSize;
						txPk->sourceId = node_id;
						txPk->messageType = DATA_FRAME;
						tx_state = TX_DATA_FRAME;
					}
				}
				else
				{
					DTR_DEBUG_PRINT("No TX DATA,forwarding token to next\n");
					txPk = &servicePk;
					txPk->messageType = TOKEN_FRAME;
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
			if (rxPk->messageType == RTS_FRAME && rxPk->sourceId == next_node_id)
			{
				DTR_DEBUG_PRINT("\nReceived TOKEN_ACK from next->sending CTS \n");
				dtrShutdownSenderTimer();
				servicePk.messageType = CTS_FRAME;
				setupRadioTx(&servicePk, TX_CTS);
				continue;
			}

			/* drop all other packets and restart receiver */
			break;

		case RX_WAIT_DATA_ACK:
			if (rxPk->messageType == DATA_ACK_FRAME && rxPk->targetId == node_id)
			{
				dtrShutdownSenderTimer();

				// TODO: bad implementation, should be fixed
				dtrPacket _txPk;
				dtrGetPacketFromQueue(&_txPk, TX_DATA_Q, M2T(TX_RECEIVED_WAIT_TIME));
				txPk = &_txPk;

				if (next_sender_id == 255)
				{
					next_target_id = getNextNodeId(node_id);
				}
				else
				{
					next_target_id = getNextNodeId(next_sender_id);
				}

				DTR_DEBUG_PRINT("next_target_id: %d\n", next_target_id);

				bool not_in_broadcast_mode = !(txPk->targetId == BROADCAST_ID);
				bool reached_desired_node = not_in_broadcast_mode && (txPk->targetId == next_target_id);

				bool reached_self = (next_target_id == node_id);

				if (reached_desired_node || reached_self)
				{
					DTR_DEBUG_PRINT("Releasing TX DATA:\n");
					// dtrPrintPacket(txPk);

					if (reached_desired_node)
					{
						DTR_DEBUG_PRINT("Reached desired node (%d), stop data sending earlier...\n", txPk->targetId);
					}
					DTR_DEBUG_PRINT("Is Q Empty: %d\n", !dtrIsPacketInQueueAvailable(TX_DATA_Q));

					dtrReleasePacketFromQueue(TX_DATA_Q);
					txPk = &servicePk;
					txPk->messageType = TOKEN_FRAME;
					tx_state = TX_TOKEN;
					next_sender_id = 255;
				}
				else
				{
					txPk->targetId = next_target_id;
					next_sender_id = next_target_id;
					DTR_DEBUG_PRINT("Sending DATA to next peer..");
					DTR_DEBUG_PRINT("with target id: %d\n", txPk->targetId);
					tx_state = TX_DATA_FRAME;
				}
				setupRadioTx(txPk, tx_state);
				continue;
			}

			/* drop all other packets and restart receiver */
			break;

		default:
			DEBUG_PRINT("\nRadio receiver state: %d not set correctly!!\n", rx_state);
			continue;
		}

		// TODO: There are cases that the packet received from queue
		// is the same to the one that was previously received and already processed.
		// Maybe when releasing the packet, also release the ones that are similar to it in the queue.

		// DEBUG_PRINT("Packet Received cannot be processed\n");
		// DEBUG_PRINT("State: %d\n", rx_state);
		// DEBUG_PRINT("Packet type: %d\n", rxPk->messageType);

		// resetProtocol(); //TODO: test if it makes sense
	}
}
// ============================================================================
uint8_t dtrGetDeviceAddress()
{
	return node_id;
}

void dtrTimeOutCallBack(xTimerHandle timer)
{
#ifdef DEBUG_DTR_PROTOCOL
	DTR_DEBUG_PRINT("Sending packet after timeout: %s", getMessageType(timerDTRpacket->messageType));
	if (timerDTRpacket->messageType == DATA_FRAME)
	{
		for (size_t i = 0; i < timerDTRpacket->dataSize; i++)
		{
			DTR_DEBUG_PRINT(" %d ", timerDTRpacket->data[i]);
		}
	}
	DTR_DEBUG_PRINT("\n");
#endif

	dtrSendP2Ppacket(timerDTRpacket);
	radioMetaInfo.sendPackets++;

	switch (tx_state)
	{
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

const dtrRadioInfo *dtrGetRadioInfo()
{
	radioMetaInfo.deviceId = node_id;
	return &radioMetaInfo;
}

void dtrResetRadioMetaInfo()
{
	memset(&radioMetaInfo, 0, sizeof(radioMetaInfo));
}

#ifdef STATIC_PREDEFINED_TOPOLOGY
static void dtrStartCommunication()
{
	dtrPacket *startSignal = &startPacket;

	startSignal->sourceId = node_id;
	startSignal->targetId = next_node_id;
	startSignal->data[0] = (uint8_t)(START_PACKET >> 8);
	startSignal->data[1] = (uint8_t)START_PACKET;
	startSignal->dataSize = 2;
	startSignal->packetSize = DTR_PACKET_HEADER_SIZE + startSignal->dataSize;
	startSignal->messageType = DATA_FRAME;
	timerDTRpacket = startSignal;

	timerRadioTxState = TX_DATA_FRAME;
	rx_state = RX_WAIT_DATA_ACK;
	DTR_DEBUG_PRINT("Spamming DATA\n");
	dtrSendPacket(startSignal);
	dtrStartSenderTimer(MAX_WAIT_TIME_FOR_DATA_ACK);
}
#endif

void dtrPrintPacket(dtrPacket *packet)
{
	DTR_DEBUG_PRINT("\nDTR Packet Received: %s\n", getMessageType(packet->messageType));
	DTR_DEBUG_PRINT("Packet Size: %d\n", packet->packetSize);
	DTR_DEBUG_PRINT("Message Type: %s\n", getMessageType(packet->messageType));
	DTR_DEBUG_PRINT("Source ID: %d\n", packet->sourceId);
	DTR_DEBUG_PRINT("Target ID: %d\n", packet->targetId);
	if (packet->dataSize > 0)
	{
		DEBUG_PRINT("Data: ");
		for (int i = 0; i < packet->dataSize; i++)
		{
			DEBUG_PRINT("%d ", packet->data[i]);
		}
		DEBUG_PRINT("\n");
	}

	DTR_DEBUG_PRINT("\n\n");
}

uint8_t dtrGetSelfId(void)
{
	// Get the current address of the crazyflie and
	// keep the last 2 digits as the id of the crazyflie.
	uint64_t address = configblockGetRadioAddress();
	uint8_t my_id = (uint8_t)((address)&0x00000000ff);
	return my_id;
}

void dtrEnableProtocol(dtrTopology topology)
{
	DTR_DEBUG_PRINT("Initializing queues ...\n");
	dtrQueueingInit();

	DTR_DEBUG_PRINT("Initializing token ring ...\n");
	initTokenRing(topology, my_id);

	DEBUG_PRINT("Initializing DTR Sender ...\n");
	dtrInitSenderTimer();

	DEBUG_PRINT("Initializing Handshake timer ...\n");
	dtrInitHandshakeTimer();

	DTR_DEBUG_PRINT("Starting protocol timer ...\n");
	dtrStartProtocolTask();

#ifdef STATIC_PREDEFINED_TOPOLOGY
	// The first node starts the protocol communication
	if (my_id == topology.devices_ids[0])
	{
		dtrStartCommunication();
	}
#else
	// start handshake timer
	dtrStartHandshakeTimer();
#endif
}

void DisableDTRProtocol(void)
{
	DTR_DEBUG_PRINT("Stopping protocol timer ...\n");
	dtrStopProtocolTask();

	DTR_DEBUG_PRINT("Stopping DTR Sender ...\n");
	dtrShutdownSenderTimer();

	DTR_DEBUG_PRINT("Resetting token ring ...\n");
	resetProtocol();

	DTR_DEBUG_PRINT("Stopping queues ...\n");
	dtrEmptyQueues();
}

bool dtrSendPacket(dtrPacket *packet)
{
	packet->messageType = DATA_FRAME;
	packet->sourceId = node_id;
	packet->packetSize = DTR_PACKET_HEADER_SIZE + packet->dataSize;
	return dtrInsertPacketToQueue(packet, TX_DATA_Q);
}

bool dtrGetPacket(dtrPacket *packet, uint32_t timeout)
{
	return dtrGetPacketFromQueue(packet, RX_DATA_Q, timeout);
}

// LOGGING VARIABLES
LOG_GROUP_START(DTR_P2P)
LOG_ADD(LOG_UINT8, rx_state, &rx_state)
LOG_ADD(LOG_UINT8, tx_state, &tx_state)
LOG_GROUP_STOP(DTR_P2P)
