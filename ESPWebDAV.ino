// Using the WebDAV server with Rigidbot 3D printer.
// Printer controller is a variation of Rambo running Marlin firmware

#include "serial.h"
#include "parser.h"
#include "config.h"
#include "network.h"
#include "gcode.h"
#include "sdControl.h"
#include <SdFat.h>
#include "pins.h"

// ==== TEMPORARY SD-ACTIVITY MONITOR (remove after profiling) ============
// Profiles the CPAP's SD-bus access via the CS_SENSE interrupt and appends an
// edge count to /SDMON.LOG once per 60 s. WiFi is NOT started in this mode, so
// nothing contends for the card beyond this brief once-a-minute log write.
// Comment out the define below to restore normal WebDAV operation.
#define SD_ACTIVITY_MONITOR
// =======================================================================

#ifdef SD_ACTIVITY_MONITOR
static SdFat monSd;

void runSdActivityMonitor() {
	SERIAL_ECHOLN("=== SD ACTIVITY MONITOR (no WiFi) ===");
	SERIAL_ECHOLN("logging CS_SENSE edge counts to /SDMON.LOG every 60s");

	sdcontrol.takeBusControl();
	bool mounted = monSd.begin(SD_CS, SPI_FULL_SPEED);
	sdcontrol.relinquishBusControl();
	if(!mounted)
		SERIAL_ECHOLN("WARN: initial SD mount failed; will retry each write");

	unsigned long window = 0;
	unsigned long bootMs = millis();
	for(;;) {
		for(int s = 0; s < 60; s++)
			delay(1000);            // ESP idle for 60s; CPAP free to use the bus

		unsigned long edges = SDControl::readAndResetEdges();
		unsigned long tsec = (millis() - bootMs) / 1000UL;
		window++;

		char line[96];
		snprintf(line, sizeof(line), "w=%lu t=%lus edges=%lu\r\n",
		         window, tsec, edges);

		// Brief bus-take just to append one line, then release.
		sdcontrol.takeBusControl();
		SdFile f;
		if(!f.open("SDMON.LOG", O_CREAT | O_WRITE | O_APPEND)) {
			monSd.begin(SD_CS, SPI_FULL_SPEED);   // remount + retry once
			f.open("SDMON.LOG", O_CREAT | O_WRITE | O_APPEND);
		}
		if(f.isOpen()) {
			f.write((const uint8_t*)line, strlen(line));
			f.sync();
			f.close();
		}
		sdcontrol.relinquishBusControl();

		SERIAL_ECHO(line);          // mirror to the open serial monitor
	}
}
#endif

// LED is connected to GPIO2 on this board
#define INIT_LED			{pinMode(2, OUTPUT);}
#define LED_ON				{digitalWrite(2, LOW);}
#define LED_OFF				{digitalWrite(2, HIGH);}

// ------------------------
void setup() {
	SERIAL_INIT(115200);
	INIT_LED;
	blink();
	
	sdcontrol.setup();

#ifdef SD_ACTIVITY_MONITOR
	runSdActivityMonitor();     // diagnostic build: profile SD bus, no WiFi
	return;
#endif

	// ----- WIFI -------
  if(config.load() == 1) { // Connected before
    if(!network.start()) {
      SERIAL_ECHOLN("Connect fail, please check your INI file or set the wifi config and connect again");
      SERIAL_ECHOLN("- M50: Set the wifi ssid , 'M50 ssid-name'");
      SERIAL_ECHOLN("- M51: Set the wifi password , 'M51 password'");
      SERIAL_ECHOLN("- M52: Start to connect the wifi");
      SERIAL_ECHOLN("- M53: Check the connection status");
    }
  }
  else {
    SERIAL_ECHOLN("Welcome to FYSETC: www.fysetc.com");
    SERIAL_ECHOLN("Please set the wifi config first");
    SERIAL_ECHOLN("- M50: Set the wifi ssid , 'M50 ssid-name'");
    SERIAL_ECHOLN("- M51: Set the wifi password , 'M51 password'");
    SERIAL_ECHOLN("- M52: Start to connect the wifi");
    SERIAL_ECHOLN("- M53: Check the connection status");
  }
}

// ------------------------
void loop() {
  // handle the request
	network.handle();

  // Handle gcode
  gcode.Handle();

  // blink
  statusBlink();
}

// ------------------------
void blink()	{
// ------------------------
	LED_ON; 
	delay(100); 
	LED_OFF; 
	delay(400);
}

// ------------------------
void errorBlink()	{
// ------------------------
	for(int i = 0; i < 100; i++)	{
		LED_ON; 
		delay(50); 
		LED_OFF; 
		delay(50);
	}
}

void statusBlink() {
  static unsigned long time = 0;
  if(millis() > time + 1000 ) {
    if(network.isConnecting()) {
      LED_OFF;
    }
    else if(network.isConnected()) {
      LED_ON; 
  		delay(50); 
  		LED_OFF; 
    }
    else {
      LED_ON;
    }
    time = millis();
  }

  // SPI bus not ready
	//if(millis() < spiBlockoutTime)
	//	blink();
}
