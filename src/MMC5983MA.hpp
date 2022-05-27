//
//  MMC5983MA.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/27/22.
//

/*
	 MMC5983MA High Performance h 3-axis Magnetic Sensor
  https://www.memsic.com/magnetometer-5
 https://www.memsic.com/Public/Uploads/uploadfile/files/20220119/MMC5983MADatasheetRevA.pdf
 
*/
 
#pragma once

#include "I2C.hpp"

using namespace std;

class MMC5983MA
{
 
public:
	MMC5983MA();
	~MMC5983MA();
 
	// Address of Temperature sensor (0x48,0x49,0x4A,0x4B)
 
	bool begin(uint8_t deviceAddress = 0x30);
	bool begin(uint8_t deviceAddress,  int &error);
	void stop();
 
	 
	uint8_t	getDevAddr();

	
	bool getChipID(uint8_t &chipID);
 
private:
 
	I2C 		_i2cPort;
	bool		_isSetup;

};
 
