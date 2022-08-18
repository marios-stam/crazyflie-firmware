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
 * p2p_interface.h
 * 
 */

#ifndef _P2P_INTERFACE_H_
#define _P2P_INTERFACE_H_

#include "FreeRTOS.h"
#include "DTR_types.h"
#include "radiolink.h"
#include "token_ring.h"

#define INCOMING_DTR_QUEUE_SIZE 10

// Broadcasts a DTR packet through the P2P network
void sendDTRpacket(const DTRpacket* packet) ;

// Puts the DTR packet in the queue for the token ring to pick up.
void p2pcallbackHandler(P2PPacket *p);

void feedDTRPacketToProtocol(DTRpacket *incoming_DTR);

#endif // _P2P_INTERFACE_H_
