#include <ESP8266WiFi.h>
#include <SdFat.h>

// Debug ring buffer: DBG_PRINT/DBG_PRINTLN route here AND mirror to Serial.
// Dumped over HTTP at  GET /debuglog  (text/plain) so the trace is readable
// with the card in the ResMed and USB serial unavailable;  GET /debugclear
// resets it. Holds the most-recent output in a wrapping RAM buffer (tunable).
#define DBG_RING_SIZE 8192

class DebugRing : public Print {
public:
	virtual size_t write(uint8_t c) {
		Serial.write(c);  // mirror to serial (isolation testing)
		_buf[_head++] = (char)c;
		if(_head >= DBG_RING_SIZE) { _head = 0; _wrapped = true; }
		return 1;
	}
	using Print::write;
	size_t      length()  const { return _wrapped ? DBG_RING_SIZE : _head; }
	const char* buf()     const { return _buf; }
	size_t      head()    const { return _head; }
	bool        wrapped() const { return _wrapped; }
	void        reset()         { _head = 0; _wrapped = false; }
private:
	char   _buf[DBG_RING_SIZE];
	size_t _head = 0;
	bool   _wrapped = false;
};
extern DebugRing dbg;

#define DEBUG

#ifdef DEBUG
	#define DBG_PRINT(...)   { dbg.print(__VA_ARGS__); }
	#define DBG_PRINTLN(...) { dbg.println(__VA_ARGS__); }
#else
	#define DBG_PRINT(...)   {}
	#define DBG_PRINTLN(...) {}
#endif

// constants for WebServer
#define CONTENT_LENGTH_UNKNOWN ((size_t) -1)
#define CONTENT_LENGTH_NOT_SET ((size_t) -2)
#define HTTP_MAX_POST_WAIT 		5000 

// GET read-chunk size: SD is read one chunk at a time under a short bus
// lease, then written to the client with the bus free. Static (BSS), not
// on the ~4KB stack.
#define GET_CHUNK                 8192

// Build fingerprint -- printed on the serial boot banner (ESPWebDAV::init)
// and emitted as an X-Firmware header on every response (_prepareHeader),
// so the running binary is verifiable over HTTP with no serial or reflash:
//   curl -sI -X OPTIONS http://<ip>/ | grep -i x-firmware
#define FW_BUILD "fysetc-espwebdav fix v10 decouple+cacheclear+ringlog 2026-09-05"

enum ResourceType { RESOURCE_NONE, RESOURCE_FILE, RESOURCE_DIR };
enum DepthType { DEPTH_NONE, DEPTH_CHILD, DEPTH_ALL };


class ESPWebDAV	{
public:
	bool init(int chipSelectPin, SPISettings spiSettings, int serverPort);
  bool initSD(int chipSelectPin, SPISettings spiSettings);
  bool startServer();
	bool isClientWaiting();
	void handleClient(String blank = "");
	void rejectClient(String rejectMessage);
	
protected:
	typedef void (ESPWebDAV::*THandlerFunction)(String);
	
	void processClient(THandlerFunction handler, String message);
	void handleNotFound();
	void handleReject(String rejectMessage);
	void handleRequest(String blank);
	void handleOptions(ResourceType resource);
	void handleLock(ResourceType resource);
	void handleUnlock(ResourceType resource);
	void handlePropPatch(ResourceType resource);
	void handleProp(ResourceType resource);
	void sendPropResponse(boolean recursing, FatFile *curFile);
	void handleGet(ResourceType resource, bool isGet);
	void handleDebugLog();
	void handleDebugClear();
	void handlePut(ResourceType resource);
	void handleWriteError(String message, FatFile *wFile);
	void handleDirectoryCreate(ResourceType resource);
	void handleMove(ResourceType resource);
	void handleDelete(ResourceType resource);

	// Sections are copied from ESP8266Webserver
	String getMimeType(String path);
	String urlDecode(const String& text);
	String urlToUri(String url);
	bool parseRequest();
	void sendHeader(const String& name, const String& value, bool first = false);
	void send(String code, const char* content_type, const String& content);
	void _prepareHeader(String& response, String code, const char* content_type, size_t contentLength);
	void sendContent(const String& content);
	void sendContent_P(PGM_P content);
	void setContentLength(size_t len);
	size_t readBytesWithTimeout(uint8_t *buf, size_t bufSize);
	size_t readBytesWithTimeout(uint8_t *buf, size_t bufSize, size_t numToRead);
	
	
	// variables pertaining to current most HTTP request being serviced
	WiFiServer *server;
	SdFat sd;
	int _sdCsPin;
	SPISettings _sdSpi;

	WiFiClient 	client;
	String 		method;
	String 		uri;
	String 		contentLengthHeader;
	String 		depthHeader;
	String 		hostHeader;
	String		destinationHeader;

	String 		_responseHeaders;
	bool		_chunked;
	int			_contentLength;
};

extern ESPWebDAV dav;
