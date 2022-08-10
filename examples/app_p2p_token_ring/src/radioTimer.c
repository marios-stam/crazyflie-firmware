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
 * radioTimer.c
 *
 *  Created on: 14.02.2021
 *      Author: Christos Zosimidis
 *
 *  Modified for the P2P protocol by: Bitcraze AB
 * 
 */


#include "radioTimer.h"
#include "token_ring.h"
#include "timers.h"

static xTimerHandle timer;
static bool timer_running = false;


void initRadioTimer() {
	timer = xTimerCreate("RadioTimer", M2T(20), pdTRUE, NULL, timeOutCallBack);
}

void setRadioTimer(unsigned int time_out) {
	xTimerChangePeriod(timer, M2T(time_out), 0);
}

void shutdownRadioTimer() {
	if (timer_running) {
		xTimerStop(timer, 0);
		timer_running = false;
	}else{
		DEBUG_PRINT("Radio timer not running\n");
	}
}

void startRadioTimer() {
	if (timer_running){
		DEBUG_PRINT("Radio timer already running\n");
	}else{
		xTimerStart(timer, 20);
		timer_running = true;
	}
}

