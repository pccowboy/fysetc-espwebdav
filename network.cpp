#include "network.h"
#include "serial.h"
#include "config.h"
#include "pins.h"
#include "ESP8266WiFi.h"
#include "ESPWebDAV.h"
#include "sdControl.h"

String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") +\
  String(ipAddress[1]) + String(".") +\
  String(ipAddress[2]) + String(".") +\
  String(ipAddress[3])  ;
}

bool Network::start() {
  wifiConnected = false;
  wifiConnecting = true;
  
  // Set hostname first
  WiFi.hostname(HOSTNAME);
  // Reduce startup surge current
  WiFi.setAutoConnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.setPhyMode(WIFI_PHY_MODE_11N);
  WiFi.begin(config.ssid(), config.password());

  // Wait for connection
  unsigned int timeout = 0;
  while(WiFi.status() != WL_CONNECTED) {
    //blink();
    SERIAL_ECHO(".");
    timeout++;
    if(timeout++ > WIFI_CONNECT_TIMEOUT/100) {
      SERIAL_ECHOLN("");
      wifiConnecting = false;
      return false;
    }
    else
      delay(100);
  }

  SERIAL_ECHOLN("");
  SERIAL_ECHO("Connected to "); SERIAL_ECHOLN(config.ssid());
  SERIAL_ECHO("IP address: "); SERIAL_ECHOLN(WiFi.localIP());
  SERIAL_ECHO("RSSI: "); SERIAL_ECHOLN(WiFi.RSSI());
  SERIAL_ECHO("Mode: "); SERIAL_ECHOLN(WiFi.getPhyMode());
  SERIAL_ECHO("Asscess to SD at the Run prompt : \\\\"); SERIAL_ECHO(WiFi.localIP());SERIAL_ECHOLN("\\DavWWWRoot");

  wifiConnected = true;

  config.save();
  String sIp = IpAddress2String(WiFi.localIP());
  config.save_ip(sIp.c_str());

  SERIAL_ECHOLN("Going to start DAV server");
  if(startDAVServer() < 0) return false;
  wifiConnecting = false;

  return true;
}

int Network::startDAVServer() {
  if(!sdcontrol.canWeTakeBus()) {
    return -1;
  }
  sdcontrol.takeBusControl();
  
  // start the SD DAV server
  if(!dav.init(SD_CS, SPI_FULL_SPEED, SERVER_PORT))   {
    DBG_PRINT("ERROR: "); DBG_PRINTLN("Failed to initialize SD Card");
    dav.printSdInitError();
    // indicate error on LED
    //errorBlink();
    initFailed = true;
  }
  else {
    //blink();
  }
  
  sdcontrol.relinquishBusControl();
  DBG_PRINTLN("FYSETC WebDAV server started");
  return 0;
}

bool Network::isConnected() {
  return wifiConnected;
}

bool Network::isConnecting() {
  return wifiConnecting;
}

// a client is waiting and FS is ready and other SPI master is not using the bus
bool Network::ready() {
  if(!isConnected()) return false;
  
  // do it only if there is a need to read FS
	if(!dav.isClientWaiting())	return false;
	
	// initFailed only reflects the ONE mount attempt at boot in
	// startDAVServer(). [v12, kept over the revert below] Not treated
	// as a permanent gate: a single transient boot-time mount failure
	// must not wedge every future request forever. main only mounts
	// once at boot and never again, so upstream never needed a retry
	// path here; canWeTakeBus() below is the live per-request signal.
	
	// [v13: restored from main] has other master been using the bus
	// in last few seconds. Re-adding the ORIGINAL blockout gate that
	// v1 removed -- the per-request dav.initSD() remount v1 added in
	// handle() (below) is suspected of being what makes the card
	// drop off the host bus as soon as WiFi activity starts.
	if(!sdcontrol.canWeTakeBus()) {
		dav.rejectClient("Marlin is reading from SD card");
		return false;
	}

	return true;
}

void Network::handle() {
  if(network.ready()) {
	  // [v13: restored from main] mount ONCE at boot (startDAVServer);
	  // no per-request remount. v1 added dav.initSD() here to fix a
	  // stale-FAT-view 404 after CPAP writes -- if that turns out to
	  // still be needed, it comes back deliberately, not as a default.
	  sdcontrol.takeBusControl();
	  dav.handleClient();
	  sdcontrol.relinquishBusControl();
	}
}

Network network;
