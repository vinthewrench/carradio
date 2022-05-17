//
//  DuppaLEDRing.cpp
//  Duppatest
//
//  Created by Vincent Moscaritolo on 5/14/22.
//

#include "DuppaLEDRing.hpp"
#include <unistd.h>

enum I2C_Register {
	COMMANDREGISTER 		= 0xFD,
	COMMANDREGISTER_LOCK = 0xFE,
	ID_REGISTER 			= 0xFC,
	ULOCK_CODE 				= 0xC5,
	CONFIGURATION 			= 0x50,
	GLOBALCURRENT 			= 0x51,
	PULLUPDOWM 				= 0x52,
	OPENSHORT 				= 0x53,
	TEMPERATURE 			= 0x5F,
	SPREADSPECTRUM			 = 0x60,
	RESET_REG 				= 0x8F,
	PWM_FREQUENCY_ENABLE = 0xE0,
	PWM_FREQUENCY_SET 	= 0xE2,
	
	PAGE0 					= 0x00,
	PAGE1 					= 0x01
} ;


#define ISSI3746_PAGE0 0x00
#define ISSI3746_PAGE1 0x01


#define ISSI3746_COMMANDREGISTER 0xFD
#define ISSI3746_COMMANDREGISTER_LOCK 0xFE
#define ISSI3746_ID_REGISTER 0xFC
#define ISSI3746_ULOCK_CODE 0xC5

#define ISSI3746_CONFIGURATION 0x50
#define ISSI3746_GLOBALCURRENT 0x51
#define ISSI3746_PULLUPDOWM 0x52
#define ISSI3746_OPENSHORT 0x53
#define ISSI3746_TEMPERATURE 0x5F
#define ISSI3746_SPREADSPECTRUM 0x60
#define ISSI3746_RESET_REG 0x8F
#define ISSI3746_PWM_FREQUENCY_ENABLE 0xE0
#define ISSI3746_PWM_FREQUENCY_SET 0xE2

DuppaLEDRing::DuppaLEDRing(){
	_isSetup = false;
}

DuppaLEDRing::~DuppaLEDRing(){
	stop();
}

bool DuppaLEDRing::begin(uint8_t deviceAddress){
	int error = 0;
	
	return begin(deviceAddress, error);
}

bool DuppaLEDRing::begin(uint8_t deviceAddress,  int &error){
	
	if( _i2cPort.begin(deviceAddress, error)
		&& setConfig(0x01) //Normal operation
		) {
		
		_isSetup = true;
	}
	
	
	return _isSetup;
}

void DuppaLEDRing::stop(){
	
	clearAll();
	GlobalCurrent(0);
	
	_isSetup = false;
	_i2cPort.stop();
	
	//	LOG_INFO("QwiicTwist(%02x) stop\n",  _i2cPort.getDevAddr());
}

uint8_t	DuppaLEDRing::getDevAddr(){
	return _i2cPort.getDevAddr();
};


// Reset the board
bool DuppaLEDRing::reset(void) {
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		
		success =	selectBank(PAGE1)
		&&  _i2cPort.writeByte(RESET_REG,0xAE);
		
		if(success){
			usleep(400);
		}
	}
	return success;
	
}


bool DuppaLEDRing::clearAll(void) {
	bool success = false;
	
	if(_i2cPort.isAvailable()
		&& PWM_MODE()) {
		
		for (int i = 1; i < 73; i++) {
			success = _i2cPort.writeByte(i, 0);
			if(!success) break;
		}
	}
	return success;
	
}

bool DuppaLEDRing::PWM_MODE(void) {
	bool success = false;
	
	success =	selectBank(PAGE0);
	
	return success;
	
}

bool DuppaLEDRing::PWMFrequencyEnable(uint8_t PWMenable) {
	bool success = false;

	success =	selectBank(PAGE1)
	&&  _i2cPort.writeByte(PWM_FREQUENCY_ENABLE,	PWMenable);
	
	
	return success;
}


bool DuppaLEDRing::SpreadSpectrum(uint8_t spread){
	bool success = false;
	
	success =	selectBank(PAGE1)
	&&  _i2cPort.writeByte(SPREADSPECTRUM,	spread);
	return success;
	
}


bool DuppaLEDRing::GlobalCurrent(uint8_t curr) {
	bool success = false;
	success =	selectBank(PAGE1)
	&&  _i2cPort.writeByte(GLOBALCURRENT,	curr);
	return success;
	
}

bool DuppaLEDRing::SetScaling(uint8_t scal) {
	bool success = false;
	
	success =	selectBank(PAGE1);
	if(success){
		for (uint8_t i = 1; i < 73; i++) {
			success = _i2cPort.writeByte(i, scal);
			if(!success) break;
		}
	}
	
	return success;
	
}



bool  DuppaLEDRing::setConfig(uint8_t b){
	bool success = false;

	success =	selectBank(PAGE1)
	&&  _i2cPort.writeByte(CONFIGURATION,	b);
	
	return success;
}



bool  DuppaLEDRing::selectBank(uint8_t b){
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		
		success =	_i2cPort.writeByte(COMMANDREGISTER_LOCK, ULOCK_CODE)
		&&  _i2cPort.writeByte(COMMANDREGISTER,	b);
	}
	
	return success;
}

bool  DuppaLEDRing::setColor(uint8_t led_n, uint8_t red, uint8_t green, uint8_t blue ){
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		success =	_i2cPort.writeByte(issi_led_map[0][led_n], red)
		&& 	_i2cPort.writeByte(issi_led_map[1][led_n], green)
		&& 	_i2cPort.writeByte(issi_led_map[2][led_n], blue);
	}
	
	return success;
}

bool  DuppaLEDRing::setRED(uint8_t led_n, uint8_t color){
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		success =	_i2cPort.writeByte(issi_led_map[0][led_n], color);
	}
	
	return success;
}

bool  DuppaLEDRing::setGREEN(uint8_t led_n, uint8_t color){
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		success =	_i2cPort.writeByte(issi_led_map[1][led_n], color);
	}
	
	return success;
}

bool  DuppaLEDRing::setBLUE(uint8_t led_n, uint8_t color){
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		success =	_i2cPort.writeByte(issi_led_map[2][led_n], color);
	}
	
	return success;
}


