//
//  MMC5983MA.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/27/22.
//

#include "MMC5983MA.hpp"
#include "CommonDefs.hpp"
#include "ErrorMgr.hpp"
#include <unistd.h>

//Register map for MMC5983MA'
//http://www.memsic.com/userfiles/files/DataSheets/Magnetic-Sensors-Datasheets/MMC5983MA_Datasheet.pdf

enum MMC5983MA_Register
{
	MMC5983MA_XOUT_0		= 0x00,
	MMC5983MA_XOUT_1		= 0x01,
	MMC5983MA_YOUT_0		= 0x02,
	MMC5983MA_YOUT_1		= 0x03,
	MMC5983MA_ZOUT_0		= 0x04,
	MMC5983MA_ZOUT_1		= 0x05,
	MMC5983MA_XYZOUT_2		= 0x06,
	MMC5983MA_TOUT			= 0x07,
	MMC5983MA_STATUS		= 0x08,
	MMC5983MA_CONTROL_0		= 0x09,
	MMC5983MA_CONTROL_1		= 0x0A,
	MMC5983MA_CONTROL_2		= 0x0B,
	MMC5983MA_CONTROL_3		= 0x0C,
	MMC5983MA_PRODUCT_ID	= 0x2F //Shouldbe0x30
};

 
MMC5983MA::MMC5983MA(){
	_isSetup = false;
}

MMC5983MA::~MMC5983MA(){
	stop();
}

bool MMC5983MA::begin(uint8_t deviceAddress){
	int error = 0;

	return begin(deviceAddress, error);
}
 
bool MMC5983MA::begin(uint8_t deviceAddress,   int &error){
 
	if( _i2cPort.begin(deviceAddress, error) ) {
		
		uint8_t  chipID = 0;
		
		if(getChipID(chipID) && chipID == 0x30) {
			_isSetup = true;
	}
		else {
			ELOG_MESSAGE("MMC5983MA(%02x) unexpected chipID = %02x\n", deviceAddress, chipID );
			error = ENODEV;
		}
		_isSetup = true;
	}

	return _isSetup;
}
 
void MMC5983MA::stop(){
	_isSetup = false;
	_i2cPort.stop();

	//	LOG_INFO("TMP117(%02x) stop\n",  _i2cPort.getDevAddr());
}
 
uint8_t	MMC5983MA::getDevAddr(){
	return _i2cPort.getDevAddr();
};




bool MMC5983MA::getChipID(uint8_t &chipID){
	
	bool success = _i2cPort.readByte(MMC5983MA_PRODUCT_ID, chipID);
	return success;

}


bool MMC5983MA::reset() {
	bool success = false;

 
		if(_i2cPort.writeByte(MMC5983MA_CONTROL_1,  (uint8_t) 0x80)){
			usleep(10000);  //  Wait 10 ms for all registers to reset
 			success = true;
		}
	 
	
	return success;
}


//uint8_t MMC5983MA::readTemperature()
//{
//  uint8_t temp = _i2c_bus->readByte(MMC5983MA_ADDRESS, MMC5983MA_TOUT);  // Read the raw temperature register
//  return temp;
//}
//

bool MMC5983MA::readTempC(float& tempOut){
	bool success = false;
	 
	uint8_t digitalTemp;
	
	printf("compass readTempC \n");
	if(_i2cPort.readByte(MMC5983MA_TOUT, digitalTemp)){
		
		float finalTempC = -75.0f + (static_cast<float>(digitalTemp) * (200.0f / 255.0f));

		tempOut = finalTempC;
		success = true;
 
		printf("readTempS  %.1f  \n",finalTempC );
	}
	return success;
}



 bool MMC5983MA::startTempMeasurement(){
	bool success = false;
	 
	 success =  _i2cPort.writeByte(MMC5983MA_CONTROL_0,  (uint8_t) 0x02);
 
	  return success;
}

bool MMC5983MA::isTempMeasurementDone() {
	
	bool isDone = false;

	uint8_t statusReg;
	
	isDone = _i2cPort.readByte(MMC5983MA_STATUS, statusReg) && ((statusReg & 0x02 ) == 0x02);
	
	printf("statusReg = %02x\n", statusReg);
 	return isDone;

}
 
