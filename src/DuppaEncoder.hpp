//
//  DuppaEncoder.hpp
//  Duppatest
//
//  Created by Vincent Moscaritolo on 5/14/22.
//

#pragma once

#include "I2C.hpp"

using namespace std;

class DuppaEncoder
{
 
public:

	/* Encoder configuration bit. Use with GCONF */
	enum GCONF_PARAMETER {
		FLOAT_DATA = 0x0001,
		INT_DATA = 0x0000,
		WRAP_ENABLE = 0x0002,
		WRAP_DISABLE = 0x0000,
		DIRE_LEFT = 0x0004,
		DIRE_RIGHT = 0x0000,
		IPUP_DISABLE = 0x0008,
		IPUP_ENABLE = 0x0000,
		RMOD_X2 = 0x0010,
		RMOD_X1 = 0x0000,
		RGB_ENCODER = 0x0020,
		STD_ENCODER = 0x0000,
		EEPROM_BANK1 = 0x0040,
		EEPROM_BANK2 = 0x0000,
		RESET = 0x0080,
		CLK_STRECH_ENABLE = 0x0100,
		CLK_STRECH_DISABLE = 0x0000,
		REL_MODE_ENABLE= 0x0200,
		REL_MODE_DISABLE = 0x0000,
	};

	
	enum STATUS_REG {
		PUSHR = 0x01,
		PUSHP = 0x02,
		PUSHD = 0x04,
		RINC = 0x08,
		RDEC = 0x10,
		RMAX = 0x20,
		RMIN = 0x40,
		INT_2 = 0x80,
 	};

	DuppaEncoder();
	~DuppaEncoder();
 
 
	bool begin(uint8_t deviceAddress, uint16_t conf = 0);
	bool begin(uint8_t deviceAddress, uint16_t conf, int &error);
	void stop();

	bool reset();
 
	// resets on read.
	bool updateStatus();
	bool updateStatus(uint8_t &regOut);
	
	bool wasClicked();	// pressed and let go
	bool wasPressed();	// still down
	bool wasMoved(bool &cw);
	
	uint8_t	getDevAddr();
  
	bool setColor(uint8_t red, uint8_t green, uint8_t blue );

	
private:
 
 
	
	I2C 		_i2cPort;
	bool		_isSetup;

	uint8_t	_lastStatus;
	
	uint8_t _clockstrech;
	uint8_t _gconf = 0x00;

};
 
