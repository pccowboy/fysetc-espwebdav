#ifndef _SD_CONTROL_H_
#define _SD_CONTROL_H_

#define SPI_BLOCKOUT_PERIOD	20000UL

class SDControl {
public:
  SDControl() { }
  static void setup();
  static void takeBusControl();
  static void relinquishBusControl();
  static bool canWeTakeBus();
  static unsigned long readAndResetEdges();
 
private:
  static volatile long _spiBlockoutTime;
  static bool _weTookBus;
  static volatile unsigned long _csSenseEdges;
};

extern SDControl sdcontrol;

#endif
