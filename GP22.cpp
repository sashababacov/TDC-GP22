#include "GP22.h"

GP22::GP22(int slaveSelectPin) {
  _ssPin = slaveSelectPin;
}

GP22::~GP22() {
  SPI.end();
}

void GP22::begin() {
  //Start up SPI
  SPI.begin(_ssPin);
  //Run the SPI clock at 14 MHz (GP22's max is apparently 20 MHz)
  SPI.setClockDivider(_ssPin, 6);
  //Clock polarity = 0, clock phase = 1 (MODE1?)
  SPI.setDataMode(_ssPin, SPI_MODE1);
  //The GP22 sends the most significant bit first
  SPI.setBitOrder(_ssPin, MSBFIRST);
  //Power-on-reset command
  SPI.transfer(_ssPin, 0x50);
  //Transfer the GP22 config registers across
  updateConfig();
}

//Initilise measurement
void GP22::measure() {
  SPI.transfer(_ssPin, 0x70);
}

uint16_t GP22::readStatus() {
  // Get the TDC status from it's stat register
  uint16_t stat = transfer2B(0xB4, 0x00, 0x00);

  // It might be worth splitting up the result into more meaningful data than just a 16 bit number.
  // These numbers could go into private variables that other functions can access.

  return stat;
}

//Function to read from result registers
uint32_t GP22::readResult(uint8_t resultRegister) {
  // Make sure that we are only reading one of the 4 possibilities
  if (resultRegister < 4 && resultRegister >= 0) {
    // The first read code is 0xB0, so add the register to get the required read code.
    uint8_t readCode = 0xB0 + resultRegister;
    return transfer4B(readCode, 0, 0, 0, 0);
  } else {
    // No such register, return 0;
    return 0;
  }
}

// These are the functions designed to make tranfers quick enough to work
// by sending the opcode and immediatly following with data (using SPI_CONTINUE).
uint8_t GP22::transfer1B(uint8_t opcode, uint8_t byte1) {
  FourByte data = { 0 };
  SPI.transfer(_ssPin, opcode, SPI_CONTINUE);
  data.bit8[0] = SPI.transfer(_ssPin, byte1);
  return data.bit8[0];
}
uint16_t GP22::transfer2B(uint8_t opcode, uint8_t byte1, uint8_t byte2) {
  FourByte data = { 0 };
  SPI.transfer(_ssPin, opcode, SPI_CONTINUE);
  data.bit8[1] = SPI.transfer(_ssPin, byte1, SPI_CONTINUE);
  data.bit8[0] = SPI.transfer(_ssPin, byte2);
  return data.bit16[0];
}
uint32_t GP22::transfer4B(uint8_t opcode, uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4) {
  FourByte data = { 0 };
  SPI.transfer(_ssPin, opcode, SPI_CONTINUE);
  data.bit8[3] = SPI.transfer(_ssPin, byte1, SPI_CONTINUE);
  data.bit8[2] = SPI.transfer(_ssPin, byte2, SPI_CONTINUE);
  data.bit8[1] = SPI.transfer(_ssPin, byte3, SPI_CONTINUE);
  data.bit8[0] = SPI.transfer(_ssPin, byte4);
  return data.bit32;
}

bool GP22::testComms() {
  // The comms can be tested by reading read register 5, which contains the highest 8 bits of config reg 1.
  int test = transfer1B(0xB5, 0);
  // Now test the result is the same as the config register (assuming the registers have been written!).
  if (test == _config[1][0]) {
    return true;
  } else {
    return false;
  }
}

float GP22::measConv(uint32_t input) {
  // Input is a Q16.16 number representation, 
  // thus conversion is via multiplication by 2^(-16).
  // The input in also multiples of the clock (4MHz).
  // Output is in microseconds.

  float qConv = pow(2.0, -16);    //Q conversion factor
  float tRef = (1.0) / (4000000.0); //4MHz clock
  float timeBase = 1000000.0;   //Microseconds

  return ((float)input) * tRef * qConv * timeBase;
}

void GP22::updateConfig() {
  //Transfer the configuration registers

  // The first config register is 0x80 and the last is 0x86
  // I know, this is a bit cheeky, but I just really wanted to try it...
  for (uint8_t i = 0; i < 7; i++)
    transfer4B((0x80 + i), _config[i][0], _config[i][1], _config[i][2], _config[i][3]);
}

//// The config setting/getting functions

// The hits of Ch1 are stored in bits 16-18 in register 1
void GP22::setExpectedHits(uint8_t hits) {
  // First lets get the bit of the config register we want to modify
  uint8_t configPiece = _config[1][1];

  // Now, we need to set and clear bits as necessary
  // In measurement mode 2, the minimum number of hits is 2 (start is included), max is 4.
  switch (hits) {
    case 2:
      bitClear(configPiece, 0);
      bitSet(configPiece, 1);
      bitClear(configPiece, 2);
      break;
    case 3:
      bitSet(configPiece, 0);
      bitSet(configPiece, 1);
      bitClear(configPiece, 2);
      break;
    case 4:
      bitClear(configPiece, 0);
      bitClear(configPiece, 1);
      bitSet(configPiece, 2);
      break;
  }

  // Now that the peice of the config that needed to be changed has been, lets put it back
  _config[1][1] = configPiece;
  // It is up to the user to update the GP22s registers
  // (in case they want to chain together setting modifications).
  // Also, so this can be called before the begin function is called.
}
uint8_t GP22::getExpectedHits() {
  return _config[1][1] & 0x07;
}

void GP22::setSingleRes(bool on) {
  uint8_t configPiece = _config[6][2];

  if (on) {
    setDoubleRes(false);
    setQuadRes(false);
  }

  _config[6][2] = configPiece;
}
bool GP22::isSingleRes() {
  return !isDoubleRes() && !isQuadRes();
}
void GP22::setDoubleRes(bool on) {
  uint8_t configPiece = _config[6][2];

  if (on) {
    bitSet(configPiece, 4);
    setQuadRes(false);
  } else {
    bitClear(configPiece, 4);
  }

  _config[6][2] = configPiece;
}
bool GP22::isDoubleRes() {
  return _config[6][2] & 0x10;
}
void GP22::setQuadRes(bool on) {
  uint8_t configPiece = _config[6][2];

  if (on) {
    bitSet(configPiece, 5);
    setDoubleRes(false);
  } else {
    bitClear(configPiece, 5);
  }

  _config[6][2] = configPiece;
}
bool GP22::isQuadRes() {
  return _config[6][2] & 0x20;
}