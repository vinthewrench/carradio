//
//  MMC5983MA.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/27/22.
//

#include "MMC5983MA.hpp"

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


