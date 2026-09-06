// WebDAV server using ESP8266 and SD card filesystem
// Targeting Windows 7 Explorer WebDav

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SdFat.h>
#include <Hash.h>
#include <time.h>
#include "ESPWebDAV.h"
#include "sdControl.h"

DebugRing dbg;

// define cal constants
const char *months[]  = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *wdays[]  = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};


// ------------------------
bool ESPWebDAV::init(int chipSelectPin, SPISettings spiSettings, int serverPort) {
// ------------------------
	// start the wifi server
	DBG_PRINT("FW_BUILD: "); DBG_PRINTLN(FW_BUILD);
	server = new WiFiServer(serverPort);
	server->begin();
	
	// initialize the SD card
	_sdCsPin = chipSelectPin;
	_sdSpi = spiSettings;
	return sd.begin(chipSelectPin, spiSettings);
}

// ------------------------
bool ESPWebDAV::initSD(int chipSelectPin, SPISettings spiSettings) {
	// initialize the SD card
	_sdCsPin = chipSelectPin;
	_sdSpi = spiSettings;
	return sd.begin(chipSelectPin, spiSettings);
}

// ------------------------
bool ESPWebDAV::startServer() {
// ------------------------
	// start the wifi server
	server->begin();
}

// ------------------------
void ESPWebDAV::handleNotFound() {
// ------------------------
	String message = "Not found\n";
	message += "URI: ";
	message += uri;
	message += " Method: ";
	message += method;
	message += "\n";

	sendHeader("Allow", "OPTIONS,MKCOL,POST,PUT");
	send("404 Not Found", "text/plain", message);
	DBG_PRINTLN("404 Not Found");
}



// ------------------------
void ESPWebDAV::handleReject(String rejectMessage)	{
// ------------------------
	DBG_PRINT("Rejecting request: "); DBG_PRINTLN(rejectMessage);

	// handle options
	if(method.equals("OPTIONS"))
		return handleOptions(RESOURCE_NONE);
	
	// handle properties
	if(method.equals("PROPFIND"))	{
		sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE");
		setContentLength(CONTENT_LENGTH_UNKNOWN);
		send("207 Multi-Status", "application/xml;charset=utf-8", "");
		sendContent(F("<?xml version=\"1.0\" encoding=\"utf-8\"?><D:multistatus xmlns:D=\"DAV:\"><D:response><D:href>/</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>Fri, 30 Nov 1979 00:00:00 GMT</D:getlastmodified><D:getetag>\"3333333333333333333333333333333333333333\"</D:getetag><D:resourcetype><D:collection/></D:resourcetype></D:prop></D:propstat></D:response>"));
		
		if(depthHeader.equals("1"))	{
			sendContent(F("<D:response><D:href>/"));
			sendContent(rejectMessage);
			sendContent(F("</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>Fri, 01 Apr 2016 16:07:40 GMT</D:getlastmodified><D:getetag>\"2222222222222222222222222222222222222222\"</D:getetag><D:resourcetype/><D:getcontentlength>0</D:getcontentlength><D:getcontenttype>application/octet-stream</D:getcontenttype></D:prop></D:propstat></D:response>"));
		}
		
		sendContent(F("</D:multistatus>"));
		return;
	}
	else
		// if reached here, means its a 404
		handleNotFound();
}




// set http_proxy=http://localhost:36036
// curl -v -X PROPFIND -H "Depth: 1" http://Rigidbot/Old/PipeClip.gcode
// Test PUT a file: curl -v -T c.txt -H "Expect:" http://Rigidbot/c.txt
// C:\Users\gsbal>curl -v -X LOCK http://Rigidbot/EMA_CPP_TRCC_Tutorial/Consumer.cpp -d "<?xml version=\"1.0\" encoding=\"utf-8\" ?><D:lockinfo xmlns:D=\"DAV:\"><D:lockscope><D:exclusive/></D:lockscope><D:locktype><D:write/></D:locktype><D:owner><D:href>CARBON2\gsbal</D:href></D:owner></D:lockinfo>"
// ------------------------
void ESPWebDAV::handleDebugLog() {
// ------------------------
	sendHeader("Allow", "GET");
	setContentLength(dbg.length());
	send("200 OK", "text/plain", "");
	// stream the ring oldest -> newest (pure RAM, bus-free)
	if(dbg.wrapped()) {
		client.write((const uint8_t*)dbg.buf() + dbg.head(), DBG_RING_SIZE - dbg.head());
		client.write((const uint8_t*)dbg.buf(), dbg.head());
	}
	else
		client.write((const uint8_t*)dbg.buf(), dbg.head());
}

// ------------------------
void ESPWebDAV::handleDebugClear() {
// ------------------------
	dbg.reset();
	send("200 OK", "text/plain", "debug ring cleared\r\n");
}

// ------------------------
void ESPWebDAV::handleRequest(String blank)	{
// ------------------------
	// Debug ring endpoints -- readable over WiFi when USB serial is unavailable
	// (card in the ResMed). Intercept before file resolution / bus lease.
	if(method.equals("GET") && uri.equals("/debuglog"))   return handleDebugLog();
	if(method.equals("GET") && uri.equals("/debugclear")) return handleDebugClear();

	ResourceType resource = RESOURCE_NONE;

	// does uri refer to a file or directory or a null?
	FatFile tFile;
	{ BusGuard _bg;
	if(tFile.open(sd.vwd(), uri.c_str(), O_READ))	{
		resource = tFile.isDir() ? RESOURCE_DIR : RESOURCE_FILE;
		tFile.close();
	}
	}

	DBG_PRINT("\r\nm: "); DBG_PRINT(method);
	DBG_PRINT(" r: "); DBG_PRINT(resource);
	DBG_PRINT(" u: "); DBG_PRINTLN(uri);

	// add header that gets sent everytime
	sendHeader("DAV", "2");

	// handle properties
	if(method.equals("PROPFIND"))
		return handleProp(resource);
	
	if(method.equals("GET"))
		return handleGet(resource, true);

	if(method.equals("HEAD"))
		return handleGet(resource, false);

	// handle options
	if(method.equals("OPTIONS"))
		return handleOptions(resource);

	// handle file create/uploads
	if(method.equals("PUT"))
		return handlePut(resource);
	
	// handle file locks
	if(method.equals("LOCK"))
		return handleLock(resource);
	
	if(method.equals("UNLOCK"))
		return handleUnlock(resource);
	
	if(method.equals("PROPPATCH"))
		return handlePropPatch(resource);
	
	// directory creation
	if(method.equals("MKCOL"))
		return handleDirectoryCreate(resource);

	// move a file or directory
	if(method.equals("MOVE"))
		return handleMove(resource);
	
	// delete a file or directory
	if(method.equals("DELETE"))
		return handleDelete(resource);

	// if reached here, means its a 404
	handleNotFound();
}



// ------------------------
void ESPWebDAV::handleOptions(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing OPTION");
	sendHeader("Allow", "PROPFIND,GET,DELETE,PUT,COPY,MOVE");
	send("200 OK", NULL, "");
}



// ------------------------
void ESPWebDAV::handleLock(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing LOCK");
	
	// does URI refer to an existing resource
	if(resource == RESOURCE_NONE)
		return handleNotFound();
	
	sendHeader("Allow", "PROPPATCH,PROPFIND,OPTIONS,DELETE,UNLOCK,COPY,LOCK,MOVE,HEAD,POST,PUT,GET");
	sendHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");

	size_t contentLen = contentLengthHeader.toInt();
	uint8_t buf[1024];
	size_t numRead = readBytesWithTimeout(buf, sizeof(buf), contentLen);
	
	if(numRead == 0)
		return handleNotFound();

	buf[contentLen] = 0;
	String inXML = String((char*) buf);
	int startIdx = inXML.indexOf("<D:href>");
	int endIdx = inXML.indexOf("</D:href>");
	if(startIdx < 0 || endIdx < 0)
		return handleNotFound();
		
	String lockUser = inXML.substring(startIdx + 8, endIdx);
	String resp1 = F("<?xml version=\"1.0\" encoding=\"utf-8\"?><D:prop xmlns:D=\"DAV:\"><D:lockdiscovery><D:activelock><D:locktype><write/></D:locktype><D:lockscope><exclusive/></D:lockscope><D:locktoken><D:href>urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9</D:href></D:locktoken><D:lockroot><D:href>");
	String resp2 = F("</D:href></D:lockroot><D:depth>infinity</D:depth><D:owner><a:href xmlns:a=\"DAV:\">");
	String resp3 = F("</a:href></D:owner><D:timeout>Second-3600</D:timeout></D:activelock></D:lockdiscovery></D:prop>");

	send("200 OK", "application/xml;charset=utf-8", resp1 + uri + resp2 + lockUser + resp3);
}



// ------------------------
void ESPWebDAV::handleUnlock(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing UNLOCK");
	sendHeader("Allow", "PROPPATCH,PROPFIND,OPTIONS,DELETE,UNLOCK,COPY,LOCK,MOVE,HEAD,POST,PUT,GET");
	sendHeader("Lock-Token", "urn:uuid:26e57cb3-834d-191a-00de-000042bdecf9");
	send("204 No Content", NULL, "");
}



// ------------------------
void ESPWebDAV::handlePropPatch(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("PROPPATCH forwarding to PROPFIND");
	handleProp(resource);
}



// ------------------------
void ESPWebDAV::handleProp(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing PROPFIND");
	// check depth header
	DepthType depth = DEPTH_NONE;
	if(depthHeader.equals("1"))
		depth = DEPTH_CHILD;
	else if(depthHeader.equals("infinity"))
		depth = DEPTH_ALL;
	
	DBG_PRINT("Depth: "); DBG_PRINTLN(depth);

	// does URI refer to an existing resource
	if(resource == RESOURCE_NONE)
		return handleNotFound();

	if(resource == RESOURCE_FILE)
		sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
	else
		sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE");

	setContentLength(CONTENT_LENGTH_UNKNOWN);
	send("207 Multi-Status", "application/xml;charset=utf-8", "");
	sendContent(F("<?xml version=\"1.0\" encoding=\"utf-8\"?>"));
	sendContent(F("<D:multistatus xmlns:D=\"DAV:\">"));

	// open this resource
	SdFile baseFile;
	{ BusGuard _bg; baseFile.open(uri.c_str(), O_READ); }
	sendPropResponse(false, &baseFile);

	if((resource == RESOURCE_DIR) && (depth == DEPTH_CHILD))	{
		// append children information to message
		SdFile childFile;
		while(true) {
			bool gotChild;
			{ BusGuard _bg; gotChild = childFile.openNext(&baseFile, O_READ); }
			if(!gotChild)
				break;
			yield();
			sendPropResponse(true, &childFile);
			{ BusGuard _bg; childFile.close(); }
		}
	}

	{ BusGuard _bg; baseFile.close(); }
	sendContent(F("</D:multistatus>"));
}



// ------------------------
void ESPWebDAV::sendPropResponse(boolean recursing, FatFile *curFile)	{
// ------------------------
	char buf[255];
	dir_t dir;
	bool curIsDir;
	uint32_t curSize;
	{
		BusGuard _bg;
		curFile->getName(buf, sizeof(buf));
		curFile->dirEntry(&dir);
		curIsDir = curFile->isDir();
		curSize = curFile->fileSize();
	}

// String fullResPath = "http://" + hostHeader + uri;
	String fullResPath = uri;

	if(recursing)
		if(fullResPath.endsWith("/"))
			fullResPath += String(buf);
		else
			fullResPath += "/" + String(buf);


	// convert to required format
	tm tmStr;
	tmStr.tm_hour = FAT_HOUR(dir.lastWriteTime);
	tmStr.tm_min = FAT_MINUTE(dir.lastWriteTime);
	tmStr.tm_sec = FAT_SECOND(dir.lastWriteTime);
	tmStr.tm_year = FAT_YEAR(dir.lastWriteDate) - 1900;
	tmStr.tm_mon = FAT_MONTH(dir.lastWriteDate) - 1;
	tmStr.tm_mday = FAT_DAY(dir.lastWriteDate);
	time_t t2t = mktime(&tmStr);
	tm *gTm = gmtime(&t2t);

	// Tue, 13 Oct 2015 17:07:35 GMT
	sprintf(buf, "%s, %02d %s %04d %02d:%02d:%02d GMT", wdays[gTm->tm_wday], gTm->tm_mday, months[gTm->tm_mon], gTm->tm_year + 1900, gTm->tm_hour, gTm->tm_min, gTm->tm_sec);
	String fileTimeStamp = String(buf);


	// send the XML information about thyself to client
	sendContent(F("<D:response><D:href>"));
	// append full file path
	sendContent(fullResPath);
	sendContent(F("</D:href><D:propstat><D:status>HTTP/1.1 200 OK</D:status><D:prop><D:getlastmodified>"));
	// append modified date
	sendContent(fileTimeStamp);
	sendContent(F("</D:getlastmodified><D:getetag>"));
	// append unique tag generated from full path
	sendContent("\"" + sha1(fullResPath + fileTimeStamp) + "\"");
	sendContent(F("</D:getetag>"));

	if(curIsDir)
		sendContent(F("<D:resourcetype><D:collection/></D:resourcetype>"));
	else	{
		sendContent(F("<D:resourcetype/><D:getcontentlength>"));
		// append the file size
		sendContent(String(curSize));
		sendContent(F("</D:getcontentlength><D:getcontenttype>"));
		// append correct file mime type
		sendContent(getMimeType(fullResPath));
		sendContent(F("</D:getcontenttype>"));
	}
	sendContent(F("</D:prop></D:propstat></D:response>"));
}




// ------------------------
void ESPWebDAV::handleGet(ResourceType resource, bool isGet)	{
// ------------------------
	DBG_PRINTLN("Processing GET");

	// does URI refer to an existing file resource
	if(resource != RESOURCE_FILE)
		return handleNotFound();

	long tStart = millis();
	static uint8_t buf[GET_CHUNK];
	size_t fileSize = 0;
	{
		BusGuard _bg;
		SdFile rFile;
		if(rFile.open(uri.c_str(), O_READ))	{
			fileSize = rFile.fileSize();
			rFile.close();
		}
	}

	sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");
	setContentLength(fileSize);
	String contentType = getMimeType(uri);
	if(uri.endsWith(".gz") && contentType != "application/x-gzip" && contentType != "application/octet-stream")
		sendHeader("Content-Encoding", "gzip");

	send("200 OK", contentType.c_str(), "");

	if(isGet) {
		// Decoupled chunked GET for the shared-bus ESP8266 module (no hardware mux, and
		// CS_SENSE cannot tell our own CS from a foreign master's while we hold the bus,
		// so mid-transfer collision detection is impossible here -- that was the v6..v8
		// otherMasterWants false-positive that discarded every good chunk). Instead:
		// read ONE chunk under a short bus lease, RELEASE the bus, then write it to the
		// client bus-free -- the slow WiFi write can never collide with the CPAP.
		// waitAndTakeBus() blocks until the CPAP has been quiet for SPI_BLOCKOUT_PERIOD
		// (armed by the CS_SENSE ISR only while we do NOT hold the bus -- the one signal
		// that is trustworthy on this hardware). cacheClear() drops stale cached FAT/dir
		// blocks on every re-take so a reopen after the CPAP mutated the card reads
		// fresh metadata (stale view == the v4 truncate-at-8192 failure).
		size_t pos = 0;
		int    chunk = 0;
		bool   ioError = false;
		int    retries = 0;
		const int MAX_RETRIES = 20;

		while(pos < fileSize) {
			int  numRead = -1;
			bool opened = false, sought = false;
			long waited, leaseMs;
			{
				long w0 = millis();
				sdcontrol.waitAndTakeBus();
				waited = millis() - w0;
				long l0 = millis();
				sd.vol()->cacheClear();
				SdFile rFile;
				opened = rFile.open(uri.c_str(), O_READ);
				if(opened) sought = rFile.seekSet(pos);
				if(sought) numRead = rFile.read(buf, sizeof(buf));
				rFile.close();
				leaseMs = millis() - l0;
				sdcontrol.relinquishBusControl();
			}

			DBG_PRINT("  chunk "); DBG_PRINT(chunk); DBG_PRINT(" pos="); DBG_PRINT(pos);
			DBG_PRINT(" wait="); DBG_PRINT(waited); DBG_PRINT("ms lease="); DBG_PRINT(leaseMs);
			DBG_PRINT("ms open="); DBG_PRINT(opened); DBG_PRINT(" seek="); DBG_PRINT(sought);
			DBG_PRINT(" read="); DBG_PRINT(numRead); DBG_PRINT(" retries="); DBG_PRINTLN(retries);

			// Bad/short read (open, seek, or read failed -- e.g. the CPAP grabbed the card
			// just as we took it): back off and retry the SAME offset on a fresh lease.
			if(numRead <= 0) {
				if(++retries > MAX_RETRIES) { DBG_PRINTLN("  GIVE UP: max retries"); ioError = true; break; }
				yield();
				continue;
			}
			retries = 0;

			// Bus is released here; the client write is the slow part and runs bus-free.
			size_t wrote = client.write(buf, numRead);
			if(wrote != (size_t)numRead) {
				DBG_PRINT("  WRITE SHORT wrote="); DBG_PRINT(wrote); DBG_PRINT("/"); DBG_PRINTLN(numRead);
				ioError = true; break;
			}
			pos += numRead;
			chunk++;
			yield();
		}

		DBG_PRINT("GET done pos="); DBG_PRINT(pos); DBG_PRINT("/"); DBG_PRINT(fileSize); DBG_PRINT(" chunks="); DBG_PRINT(chunk); DBG_PRINT(" ioError="); DBG_PRINTLN(ioError);
		if(ioError || pos < fileSize) {
			client.flush();
			client.stop();
		}
	}

	DBG_PRINT("File "); DBG_PRINT(fileSize); DBG_PRINT(" bytes sent in: "); DBG_PRINT((millis() - tStart)/1000); DBG_PRINTLN(" sec");
}




// ------------------------
void ESPWebDAV::handlePut(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing Put");
	BusGuard _bg;

	// does URI refer to a directory
	if(resource == RESOURCE_DIR)
		return handleNotFound();

	SdFile nFile;
	sendHeader("Allow", "PROPFIND,OPTIONS,DELETE,COPY,MOVE,HEAD,POST,PUT,GET");

	// if file does not exist, create it
	if(resource == RESOURCE_NONE)	{
		if(!nFile.open(uri.c_str(), O_CREAT | O_WRITE))
			return handleWriteError("Unable to create a new file", &nFile);
	}

	// file is created/open for writing at this point
	DBG_PRINT(uri); DBG_PRINTLN(" - ready for data");
	// did server send any data in put
	size_t contentLen = contentLengthHeader.toInt();

	if(contentLen != 0)	{
		// buffer size is critical *don't change*
		const size_t WRITE_BLOCK_CONST = 512;
		uint8_t buf[WRITE_BLOCK_CONST];
		long tStart = millis();
		size_t numRemaining = contentLen;

		// high speed raw write implementation
		// close any previous file
		nFile.close();
		// delete old file
		sd.remove(uri.c_str());
	
		// create a contiguous file
		size_t contBlocks = (contentLen/WRITE_BLOCK_CONST + 1);
		uint32_t bgnBlock, endBlock;

		if (!nFile.createContiguous(sd.vwd(), uri.c_str(), contBlocks * WRITE_BLOCK_CONST))
			return handleWriteError("File create contiguous sections failed", &nFile);

		// get the location of the file's blocks
		if (!nFile.contiguousRange(&bgnBlock, &endBlock))
			return handleWriteError("Unable to get contiguous range", &nFile);

		if (!sd.card()->writeStart(bgnBlock, contBlocks))
			return handleWriteError("Unable to start writing contiguous range", &nFile);

		// read data from stream and write to the file
		while(numRemaining > 0)	{
			size_t numToRead = (numRemaining > WRITE_BLOCK_CONST) ? WRITE_BLOCK_CONST : numRemaining;
			size_t numRead = readBytesWithTimeout(buf, sizeof(buf), numToRead);
			if(numRead == 0)
				break;

			// store whole buffer into file regardless of numRead
			if (!sd.card()->writeData(buf))
				return handleWriteError("Write data failed", &nFile);

			// reduce the number outstanding
			numRemaining -= numRead;
		}

		// stop writing operation
		if (!sd.card()->writeStop())
			return handleWriteError("Unable to stop writing contiguous range", &nFile);

		// detect timeout condition
		if(numRemaining)
			return handleWriteError("Timed out waiting for data", &nFile);

		// truncate the file to right length
		if(!nFile.truncate(contentLen))
			return handleWriteError("Unable to truncate the file", &nFile);

		DBG_PRINT("File "); DBG_PRINT(contentLen - numRemaining); DBG_PRINT(" bytes stored in: "); DBG_PRINT((millis() - tStart)/1000); DBG_PRINTLN(" sec");
	}

	if(resource == RESOURCE_NONE)
		send("201 Created", NULL, "");
	else
		send("200 OK", NULL, "");

	nFile.close();
}




// ------------------------
void ESPWebDAV::handleWriteError(String message, FatFile *wFile)	{
// ------------------------
	// close this file
	wFile->close();
	// delete the wrile being written
	sd.remove(uri.c_str());
	// send error
	send("500 Internal Server Error", "text/plain", message);
	DBG_PRINTLN(message);
}


// ------------------------
void ESPWebDAV::handleDirectoryCreate(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing MKCOL");
	BusGuard _bg;
	
	// does URI refer to anything
	if(resource != RESOURCE_NONE)
		return handleNotFound();
	
	// create directory
	if (!sd.mkdir(uri.c_str(), true)) {
		// send error
		send("500 Internal Server Error", "text/plain", "Unable to create directory");
		DBG_PRINTLN("Unable to create directory");
		return;
	}

	DBG_PRINT(uri);	DBG_PRINTLN(" directory created");
	sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
	send("201 Created", NULL, "");
}



// ------------------------
void ESPWebDAV::handleMove(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing MOVE");
	BusGuard _bg;
	
	// does URI refer to anything
	if(resource == RESOURCE_NONE)
		return handleNotFound();

	if(destinationHeader.length() == 0)
		return handleNotFound();
	
	String dest = urlToUri(destinationHeader);
		
	DBG_PRINT("Move destination: "); DBG_PRINTLN(dest);

	// move file or directory
	if ( !sd.rename(uri.c_str(), dest.c_str())	) {
		// send error
		send("500 Internal Server Error", "text/plain", "Unable to move");
		DBG_PRINTLN("Unable to move file/directory");
		return;
	}

	DBG_PRINTLN("Move successful");
	sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
	send("201 Created", NULL, "");
}




// ------------------------
void ESPWebDAV::handleDelete(ResourceType resource)	{
// ------------------------
	DBG_PRINTLN("Processing DELETE");
	BusGuard _bg;
	
	// does URI refer to anything
	if(resource == RESOURCE_NONE)
		return handleNotFound();

	bool retVal;
	
	if(resource == RESOURCE_FILE)
		// delete a file
		retVal = sd.remove(uri.c_str());
	else
		// delete a directory
		retVal = sd.rmdir(uri.c_str());
		
	if(!retVal)	{
		// send error
		send("500 Internal Server Error", "text/plain", "Unable to delete");
		DBG_PRINTLN("Unable to delete file/directory");
		return;
	}

	DBG_PRINTLN("Delete successful");
	sendHeader("Allow", "OPTIONS,MKCOL,LOCK,POST,PUT");
	send("200 OK", NULL, "");
}

ESPWebDAV dav;
