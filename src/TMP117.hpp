
#pragma once

#include "I2C.hpp"

using namespace std;

class TMP117
{
 
public:
	TMP117();
	~TMP117();
 
	// Address of Temperature sensor (0x48,0x49,0x4A,0x4B)
 
	bool begin(uint8_t deviceAddress = 0x48);
	bool begin(uint8_t deviceAddress,  int &error);
	void stop();
 
	bool readTempC(float&);
	bool readTempF(float&);
	
	uint8_t	getDevAddr();

private:
 
	I2C 		_i2cPort;
	bool		_isSetup;

};
 
