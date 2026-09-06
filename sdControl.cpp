#include <ESP8266WiFi.h>
#include "sdControl.h"
#include "pins.h"

volatile long SDControl::_spiBlockoutTime = 0;
volatile bool SDControl::_otherMasterWants = false;
bool SDControl::_weTookBus = false;

void SDControl::setup() {
  // ----- GPIO -------
	// Detect when other master uses SPI bus
	pinMode(CS_SENSE, INPUT);
	attachInterrupt(CS_SENSE, []() {
		if(!_weTookBus)
			_spiBlockoutTime = millis() + SPI_BLOCKOUT_PERIOD;
		else
			_otherMasterWants = true;	// CPAP asserted CS while we held the bus
	}, FALLING);

	// wait for other master to assert SPI bus first
	delay(SPI_STARTUP_DELAY);
}

// ------------------------
void SDControl::takeBusControl()	{
// ------------------------
	_weTookBus = true;
	//LED_ON;
	pinMode(MISO_PIN, SPECIAL);	
	pinMode(MOSI_PIN, SPECIAL);	
	pinMode(SCLK_PIN, SPECIAL);	
	pinMode(SD_CS, OUTPUT);
}

// ------------------------
void SDControl::relinquishBusControl()	{
// ------------------------
	pinMode(MISO_PIN, INPUT);	
	pinMode(MOSI_PIN, INPUT);	
	pinMode(SCLK_PIN, INPUT);	
	pinMode(SD_CS, INPUT);
	//LED_OFF;
	_weTookBus = false;
}

bool SDControl::canWeTakeBus() {
	if(millis() < _spiBlockoutTime) {
    return false;
  }
  // Time-based debounce alone is not enough: it only re-arms on a
  // CS_SENSE FALLING edge, so a single CPAP transaction whose CS-low
  // period runs LONGER than SPI_BLOCKOUT_PERIOD looks "clear" while
  // the CPAP is still mid-transaction -- there is no further edge left
  // to re-arm on. Read the live pin as ground truth the timer cannot
  // provide: if the other master is STILL holding CS low right now,
  // at the exact instant we decide, say so and re-arm from here.
  if(digitalRead(CS_SENSE) == LOW) {
    _spiBlockoutTime = millis() + SPI_BLOCKOUT_PERIOD;
    return false;
  }
  return true;
}

// Block until the bus has been quiet for SPI_BLOCKOUT_PERIOD (no CPAP access),
// then take it. Bounded (~2s) so a busy in-session card cannot wedge the request
// loop forever; out of session the ~10s poll gap clears the guard almost at once.
void SDControl::waitAndTakeBus() {
	unsigned long start = millis();
	while(!canWeTakeBus()) {
		yield();
		delay(2);
		if(millis() - start > 2000UL)
			break;
	}
	takeBusControl();
}
