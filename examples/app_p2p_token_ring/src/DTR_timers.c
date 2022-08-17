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
 * DTR_timers.c
 *
 *  Created on: 14.02.2021
 *      Author: Christos Zosimidis
 *
 *  Modified for the P2P protocol by: Bitcraze AB
 * 
 */


#include "DTR_timers.h"

static xTimerHandle sender_timer;
// static xTimerHandle protocol_timer;

static bool sender_timer_running = false;

static char type_to_spam[15];

void initTimers(void) {
	initDTRProtocolTimer();
	initDTRSenderTimer();
}


// =============== DTR protocol timer ===============

void initDTRProtocolTimer(void){
	// protocol_timer = xTimerCreate("DTRProtTimer", M2T(DTR_PROTOCOL_PERIOD), pdTRUE, NULL, DTRInterruptHandler);
}


void startDTRProtocol(void){
	// xTimerStart(protocol_timer, 20);
	// xTaskCreate(task, "activeMarkerDeck",configMINIMAL_STACK_SIZE, NULL, 3, NULL);

	xTaskCreate(DTRInterruptHandler, "DTR_P2P", DTR_PROTOCOL_TASK_STACK_SIZE, NULL,DTR_PROTOCOL_TASK_PRIORITY, NULL);
}


// ================ DTR sender timer ==================

void initDTRSenderTimer(void) {
	sender_timer = xTimerCreate("DTRSenderTimer", M2T(20), pdTRUE, NULL, timeOutCallBack);
}




void shutdownDTRSenderTimer(void) {
	if (xTimerIsTimerActive(sender_timer)==pdTRUE) {
		xTimerStop(sender_timer, 0);
		DTR_DEBUG_PRINT("Stopped spamming messages\n");
		sender_timer_running = false;
	}else{
		DEBUG_PRINT("Radio timer not running\n");
	}
}


void startDTRSenderTimer(unsigned int time_out) {

	if(time_out == MAX_WAIT_TIME_FOR_RTS ){
		strcpy(type_to_spam, "RTS");
	}else if (time_out == MAX_WAIT_TIME_FOR_CTS ){
		strcpy(type_to_spam, "CTS");
	}else if (time_out == MAX_WAIT_TIME_FOR_DATA_ACK ){
		strcpy(type_to_spam, "DATA");
	}

	if (sender_timer_running){
		DTR_DEBUG_PRINT("Radio timer already running\n");
	}else{
		#ifdef DEBUG_DTR_PROTOCOL
		DTR_DEBUG_PRINT("Started spamming %s\n", type_to_spam);
		#endif

		xTimerStart(sender_timer, 20);
		// xTimerChangePeriod(sender_timer, M2T(time_out), 0);

		sender_timer_running = true;
	}
}
