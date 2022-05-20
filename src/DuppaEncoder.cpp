//
//  DuppaEncoder.cpp
//  Duppatest
//
//  Created by Vincent Moscaritolo on 5/14/22.
//

#include "DuppaEncoder.hpp"
#include <unistd.h>

/*Encoder register definition*/
enum I2C_Register {
	REG_GCONF = 0x00,
	REG_GP1CONF = 0x01,
	REG_GP2CONF = 0x02,
	REG_GP3CONF = 0x03,
	REG_INTCONF = 0x04,
	REG_ESTATUS = 0x05,
	REG_I2STATUS = 0x06,
	REG_FSTATUS = 0x07,
	REG_CVALB4 = 0x08,
	REG_CVALB3 = 0x09,
	REG_CVALB2 = 0x0A,
	REG_CVALB1 = 0x0B,
	REG_CMAXB4 = 0x0C,
	REG_CMAXB3 = 0x0D,
	REG_CMAXB2 = 0x0E,
	REG_CMAXB1 = 0x0F,
	REG_CMINB4 = 0x10,
	REG_CMINB3 = 0x11,
	REG_CMINB2 = 0x12,
	REG_CMINB1 = 0x13,
	REG_ISTEPB4 = 0x14,
	REG_ISTEPB3 = 0x15,
	REG_ISTEPB2 = 0x16,
	REG_ISTEPB1 = 0x17,
	REG_RLED = 0x18,
	REG_GLED = 0x19,
	REG_BLED = 0x1A,
	REG_GP1REG = 0x1B,
	REG_GP2REG = 0x1C,
	REG_GP3REG = 0x1D,
	REG_ANTBOUNC = 0x1E,
	REG_DPPERIOD = 0x1F,
	REG_FADERGB = 0x20,
	REG_FADEGP = 0x21,
	REG_GAMRLED = 0x27,
	REG_GAMGLED = 0x28,
	REG_GAMBLED = 0x29,
	REG_GAMMAGP1 = 0x2A,
	REG_GAMMAGP2 = 0x2B,
	REG_GAMMAGP3 = 0x2C,
	REG_GCONF2 = 0x30,
	REG_IDCODE = 0x70,
	REG_VERSION = 0x71,
	REG_EEPROMS = 0x80,
} ;



DuppaEncoder::DuppaEncoder(){
	_isSetup = false;
}

DuppaEncoder::~DuppaEncoder(){
	stop();
}

bool DuppaEncoder::begin(uint8_t deviceAddress, uint16_t conf, uint8_t intConf){
	int error = 0;
	
	return begin(deviceAddress,  conf,intConf,  error);
}

bool DuppaEncoder::begin(uint8_t deviceAddress, uint16_t conf, uint8_t intConf, int &error){
	
	if( _i2cPort.begin(deviceAddress, error)
		&& _i2cPort.writeByte(REG_GCONF,  (uint8_t) 0x80)   // reset the device
		) {
		
		// wait for reset to stablize
		usleep(400);
		
		if( _i2cPort.writeByte(REG_GCONF,  (uint8_t)( conf & 0xFF))
			&& _i2cPort.writeByte(REG_GCONF2,   (uint8_t)((conf >> 8) & 0xFF))
			&& _i2cPort.writeByte(REG_INTCONF,   intConf))
		{
			_gconf = conf;
			if ((conf & CLK_STRECH_ENABLE) == 0)
				_clockstrech = 0;
			else
				_clockstrech = 1;
			
			_isSetup = true;
		}
	}
	return _isSetup;
}

void DuppaEncoder::stop(){
	reset();
//	setColor(0, 0, 0);
	_isSetup = false;
	_i2cPort.stop();
 }

uint8_t	DuppaEncoder::getDevAddr(){
	return _i2cPort.getDevAddr();
};


// Reset the board
bool DuppaEncoder::reset(void) {
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		
		if(_i2cPort.writeByte(REG_GCONF,  (uint8_t) 0x80)){
			usleep(400);
			
			success = true;
		}
	}
	return success;
}

bool DuppaEncoder::updateStatus(){
	uint8_t status;
	return updateStatus(status);
}


bool DuppaEncoder::updateStatus(uint8_t &statusOut) {
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		uint8_t status = 0;
		
		if(_i2cPort.readByte(REG_ESTATUS, status)){
			
			_lastStatus = status;
			statusOut = status;
			
			success = true;
		}
	}
	return success;
}

bool DuppaEncoder::wasClicked() {
	return  (_lastStatus & PUSHR) != 0;
}

bool DuppaEncoder::wasPressed() {
	return  (_lastStatus & PUSHP) != 0;
}

bool DuppaEncoder::wasMoved(bool &cw) {
	
	if( (_lastStatus & (RINC | RDEC)) != 0){
		cw = (_lastStatus &  RDEC) != 0;
		return true;
	}
	
	return false;
}


bool DuppaEncoder::setColor(uint8_t red, uint8_t green, uint8_t blue){
	
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		
		I2C::i2c_block_t block = {red, green, blue};
		
		success = _i2cPort.writeBlock(REG_RLED, 3, block);
	}
	
	return success;
}
