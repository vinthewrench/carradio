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
 
	 
	bool begin(uint8_t deviceAddress = 0x30);
	bool begin(uint8_t deviceAddress,  int &error);
	void stop();
 
	bool reset(); 	// reset from timeout

	 
	uint8_t	getDevAddr();

	
	bool getChipID(uint8_t &chipID);

	bool startTempMeasurement();
	bool isTempMeasurementDone();
	bool readTempC(float&);
	
	bool startMagMeasurement();
	bool isMagMeasurementDone();
	bool readMag();

private:
 
	I2C 		_i2cPort;
	bool		_isSetup;

};
 
