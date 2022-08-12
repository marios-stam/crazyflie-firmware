/**
 * ,---------,       ____  _ __
 * |  ,-^-,  |      / __ )(_) /_______________ _____  ___
 * | (  O  ) |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * | / ,--´  |    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *    +------`   /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie control firmware
 *
 * Copyright (C) 2019 Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * peer_to_peer.c - App layer application of simple demonstartion peer to peer
 *  communication. Two crazyflies need this program in order to send and receive.
 */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "app.h"

#include "FreeRTOS.h"
#include "task.h"

#include "radiolink.h"
#include "configblock.h"

#define DEBUG_MODULE "P2P"
#include "debug.h"

#include "token_ring.h"

#define NETWORK_SIZE 2 

uint8_t get_self_id(void){
	// Get the current address of the crazyflie and 
	//keep the last 2 digits as the id of the crazyflie.
	uint64_t address = configblockGetRadioAddress();
	uint8_t my_id = (uint8_t)((address)&0x00000000ff);
	return my_id;
}

void appMain(){
	DEBUG_PRINT("Waiting for activation ...\n");

	// Initialize the p2p packet
	static P2PPacket p_reply;
	p_reply.port = 0x00;

	uint8_t my_id = get_self_id();

	DEBUG_PRINT("Initializing queues ...\n");
	queueing_init();

	DEBUG_PRINT("Initializing token ring ...\n");
	initTokenRing(NETWORK_SIZE, my_id);
	
	DEBUG_PRINT("Initializing timers ...\n");
	initTimers();

	DEBUG_PRINT("Starting protocol timer ...\n");
	startDTRProtocolTimer();

	// Register the callback function so that the CF can receive packets as well.
	p2pRegisterCB(p2pcallbackHandler);

	if (my_id == 0){
		DEBUG_PRINT("Starting communication...\n");
		startRadioCommunication();

		DTRpacket  testSignal;
		testSignal.message_type = DATA_FRAME;
		testSignal.source_id = my_id;
		testSignal.target_id = 1;
		testSignal.data[0] = 66;
		testSignal.dataSize = 1;
		testSignal.packetSize = DTR_PACKET_HEADER_SIZE + testSignal.dataSize;

		bool res = sendTX_DATA_packet(&testSignal);
		if (res){
			DEBUG_PRINT("TX Packet sent to TX_DATA Q\n");
		}
		else{
			DEBUG_PRINT("Packet not sent to TX_DATA Q\n");
		}

		res = sendTX_DATA_packet(&testSignal);
		if (res){
			DEBUG_PRINT("TX Packet sent to TX_DATA Q\n");
		}
		else{
			DEBUG_PRINT("Packet not sent to TX_DATA Q\n");
		}

	}	

	DTRpacket received_packet;
	while(1){
		getRX_DATA_packet(&received_packet);
		DEBUG_PRINT("Received packet from other peer: %d\n", received_packet.data[0]);
	}
}
