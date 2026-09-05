#ifndef _SD_CONTROL_H_
#define _SD_CONTROL_H_

// Guard after the other SPI master (the CPAP) last touched the bus before the
// ESP may take it. The CPAP polls the card ~every 10s out of session; its own
// transaction is milliseconds, so a short guard clears it while leaving ~9s of
// runway. The original 20000 (> the ~10s poll interval) meant the blockout was
// re-armed before it ever expired, so the ESP could never legally take the bus.
#define SPI_BLOCKOUT_PERIOD	300UL

class SDControl {
public:
  SDControl() { }
  static void setup();
  static void takeBusControl();
  static void relinquishBusControl();
  static bool canWeTakeBus();
  static void waitAndTakeBus();
 
private:
  static volatile long _spiBlockoutTime;
  static bool _weTookBus;
};

extern SDControl sdcontrol;

// Scoped SPI-bus lease. Waits for a clear window (canWeTakeBus), takes the bus
// on construction, releases it on destruction -- so the bus is held only for
// the lifetime of the enclosing block (one SD access), and every early return
// still releases it. Keep these scopes tight around SdFat calls only; never
// hold one across network I/O.
struct BusGuard {
	BusGuard()  { sdcontrol.waitAndTakeBus(); }
	~BusGuard() { sdcontrol.relinquishBusControl(); }
};

#endif
