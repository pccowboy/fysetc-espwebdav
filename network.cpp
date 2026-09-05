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
	
	if(initFailed) {
	  dav.rejectClient("Failed to initialize SD Card");
	  return false;
	}

	// Bus arbitration is no longer decided here: a transient in-flight CPAP
	// access would otherwise reject (and consume) the whole request. Each SD
	// access below instead waits for a clear window via BusGuard, so we commit
	// to serving a waiting client and block briefly per SD op.

	return true;
}

void Network::handle() {
  if(network.ready()) {
	  // Remount the FAT volume before serving. This bridge shares one SD card
	  // with the CPAP (the other SPI master); the CPAP's writes invalidate the
	  // volume state cached at boot, making subdirectory open() fail as a
	  // spurious 404. Re-running sd.begin() gives the ESP a current view each
	  // request. The bus is held ONLY for the remount here; handleClient()
	  // then runs bus-free and re-takes it (via BusGuard) around each single
	  // SD access, so slow network I/O never holds the shared SPI bus.
	  bool mounted;
	  { BusGuard _bg; mounted = dav.initSD(SD_CS, SPI_FULL_SPEED); }
	  if(mounted)
	    dav.handleClient();
	  else
	    dav.rejectClient("Failed to initialize SD Card");
	}
}

Network network;
