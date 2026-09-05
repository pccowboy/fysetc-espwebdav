#ifndef _SD_CONTROL_H_
#define _SD_CONTROL_H_

// Guard after the other SPI master (the CPAP) last touched the bus before the
// ESP may take it. The CPAP polls the card ~every 10s out of session; its own
// transaction is milliseconds, so a short guard clears it while leaving ~9s of
// runway. The original 20000 (> the ~10s poll interval) meant the blockout was
// re-armed before it ever expired, so the ESP could never legally take the bus.
#define SPI_BLOCKOUT_PERIOD	300UL

// Startup settle: at power-on wait this long for the CPAP (the other SPI
// master) to assert the bus and initialise the card FIRST, before the ESP
// ever drives the shared pins. Must stay long -- a short value here makes the
// CPAP's own power-on card init collide with the ESP and throw
// "SD card error, remove your card". (Previously coupled to the blockout.)
#define SPI_STARTUP_DELAY	20000UL

class SDControl {
public:
  SDControl() { }
  static void setup();
  static void takeBusControl();
  static void relinquishBusControl();
  static bool canWeTakeBus();
  static void waitAndTakeBus();
  // Latched by the CS_SENSE ISR when the CPAP asserts its own CS while WE hold
  // the bus -- i.e. a real collision (not the idle-poll blockout path). The GET
  // loop polls this to yield the bus, remount, and retry the colliding chunk.
  static bool otherMasterWants()      { return _otherMasterWants; }
  static void clearOtherMasterWants() { _otherMasterWants = false; }

private:
  static volatile long _spiBlockoutTime;
  static volatile bool _otherMasterWants;
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
