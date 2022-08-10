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

#define NETWORK_SIZE 3 

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

	initTokenRing(NETWORK_SIZE, my_id);
	initRadioTimer();

	// Register the callback function so that the CF can receive packets as well.
	p2pRegisterCB(p2pcallbackHandler);

	while (1)
	{
		// Send a message every 2 seconds
		//   Note: if they are sending at the exact same time, there will be message collisions,
		//    however since they are sending every 2 seconds, and they are not started up at the same
		//    time and their internal clocks are different, there is not really something to worry about

		vTaskDelay(M2T(2000));
		radiolinkSendP2PPacketBroadcast(&p_reply);
	}
}
