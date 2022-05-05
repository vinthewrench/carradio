//
//  TMP117.cpp
//  coopMgr
//
//  Created by Vincent Moscaritolo on 9/10/21.
//

#include "TMP117.hpp"


enum TMP117_Register
{
  TMP117_TEMP_RESULT = 0X00,
  TMP117_CONFIGURATION = 0x01,
  TMP117_T_HIGH_LIMIT = 0X02,
  TMP117_T_LOW_LIMIT = 0X03,
  TMP117_EEPROM_UL = 0X04,
  TMP117_EEPROM1 = 0X05,
  TMP117_EEPROM2 = 0X06,
  TMP117_TEMP_OFFSET = 0X07,
  TMP117_EEPROM3 = 0X08,
  TMP117_DEVICE_ID = 0X0F
};

#define DEVICE_ID_VALUE 0x0117			// Value found in the device ID register on reset (page 24 Table 3 of datasheet)
#define TMP117_RESOLUTION 0.0078125f	// Resolution of the device, found on (page 1 of datasheet)

typedef union {
	struct
	{
		uint16_t DID : 12; // Indicates the device ID
		uint8_t REV : 4;   // Indicates the revision number
	} DEVICE_ID_FIELDS;
	uint16_t DEVICE_ID_COMBINED;
} DEVICE_ID_REG;

TMP117::TMP117(){
	_isSetup = false;
}

TMP117::~TMP117(){
	stop();
}

bool TMP117::begin(uint8_t deviceAddress){
	int error = 0;

	return begin(deviceAddress, error);
}
 
bool TMP117::begin(uint8_t deviceAddress,   int &error){
 
	if( _i2cPort.begin(deviceAddress, error) ) {
		_isSetup = true;
	}

	return _isSetup;
}
 
void TMP117::stop(){
	_isSetup = false;
	_i2cPort.stop();

	//	LOG_INFO("TMP117(%02x) stop\n",  _i2cPort.getDevAddr());
}
 
uint8_t	TMP117::getDevAddr(){
	return _i2cPort.getDevAddr();
};

bool TMP117::readTempF(float& tempOut){
	
	float cTemp;
	bool status = readTempC(cTemp);
	
	if(status)
		tempOut = cTemp *9.0/5.0 + 32.0;
	
	return status;
};


bool TMP117::readTempC(float& tempOut){
	bool success = false;

	union
	{
	  uint8_t  bytes[2] ;
	  uint16_t word ;
	} data;
 
 
	if(_i2cPort.readWord(TMP117_TEMP_RESULT, data.word)){
		
		int16_t digitalTemp;

		digitalTemp = ((data.bytes[0]) << 8) | (data.bytes[1] );

		float finalTempC = digitalTemp * TMP117_RESOLUTION; // Multiplies by the resolution for digital to final temp
 
		tempOut = finalTempC;
		success = true;
		
	//	printf("readTempC %04x %2.2f\n",data.word, finalTempC);
	}
	return success;
}

