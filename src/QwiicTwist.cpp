//
//  QwiicTwist.cpp
//  vfdtest
//
//  Created by Vincent Moscaritolo on 4/13/22.
//

#include "QwiicTwist.hpp"
#include <unistd.h>

//Map to the various registers on the Twist
enum encoderRegisters
{
	TWIST_ID = 0x00,
	TWIST_STATUS = 0x01, //2 - button clicked, 1 - button pressed, 0 - encoder moved
	TWIST_VERSION = 0x02,
	TWIST_ENABLE_INTS = 0x04, //1 - button interrupt, 0 - encoder interrupt
	TWIST_COUNT = 0x05,
	TWIST_DIFFERENCE = 0x07,
	TWIST_LAST_ENCODER_EVENT = 0x09, //Millis since last movement of knob
	TWIST_LAST_BUTTON_EVENT = 0x0B,  //Millis since last press/release
	
	TWIST_RED = 0x0D,
	TWIST_GREEN = 0x0E,
	TWIST_BLUE = 0x0F,
	
	TWIST_CONNECT_RED = 0x10, //Amount to change red LED for each encoder tick
	TWIST_CONNECT_GREEN = 0x12,
	TWIST_CONNECT_BLUE = 0x14,
	
	TWIST_TURN_INT_TIMEOUT = 0x16,
	TWIST_CHANGE_ADDRESS = 0x18,
	TWIST_LIMIT = 0x19,
};

constexpr uint8_t statusButtonClickedBit = 2;
constexpr uint8_t statusButtonPressedBit = 1;
constexpr uint8_t statusEncoderMovedBit = 0;

//constexpr uint8_t enableInterruptButtonBit = 1;
//constexpr uint8_t enableInterruptEncoderBit = 0;


QwiicTwist::QwiicTwist(){
	_isSetup = false;
}

QwiicTwist::~QwiicTwist(){
	stop();
}

bool QwiicTwist::begin(uint8_t deviceAddress){
	int error = 0;
	
	return begin(deviceAddress, error);
}

bool QwiicTwist::begin(uint8_t deviceAddress,   int &error){
	
	if( _i2cPort.begin(deviceAddress, error)
		&& _i2cPort.writeWord(TWIST_COUNT, 0)
		&& _i2cPort.writeByte(TWIST_STATUS,  0) ) {
		_isSetup = true;
	}
	
 
	return _isSetup;
}

void QwiicTwist::stop(){
	_isSetup = false;
	_i2cPort.stop();
	
	//	LOG_INFO("QwiicTwist(%02x) stop\n",  _i2cPort.getDevAddr());
}

uint8_t	QwiicTwist::getDevAddr(){
	return _i2cPort.getDevAddr();
};



bool QwiicTwist::getCount(int16_t &val){
	
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		int16_t  word = 0;
		if(_i2cPort.readWord(TWIST_COUNT, word)){
			val = word;
			success = true;
		}
	}
	
	return success;
}

bool QwiicTwist::setCount(int16_t val){
	
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		success = _i2cPort.writeWord(TWIST_COUNT, val);
	}
	
	return success;
}


bool QwiicTwist::getLimit(uint16_t &val){
	
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		success = _i2cPort.readWord(TWIST_LIMIT, val);
	}
	
	return success;
}

bool QwiicTwist::setLimit(uint16_t val){
	
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		success = _i2cPort.writeWord(TWIST_LIMIT, val);
	}
	
	return success;
}


bool QwiicTwist::getDiff(int16_t &val, bool clearValue ){
	
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		int16_t  word = 0;
		if(_i2cPort.readWord(TWIST_DIFFERENCE, word)){
			val = word;
			
			if (clearValue == true)
				_i2cPort.writeWord(TWIST_DIFFERENCE, 0);
			success = true;
		}
	}
	
	return success;
}


bool QwiicTwist::isPressed(bool& val){
	
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		uint8_t status = 0;
		if(_i2cPort.readByte(TWIST_STATUS, status)){
			val = status & (1 << statusButtonPressedBit);
			
			_i2cPort.writeByte(TWIST_STATUS,  status & ~(1 << statusButtonPressedBit));
		}
		
		success = true;
	}
	
	return success;
}

bool QwiicTwist::isClicked(bool& val){
	
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		uint8_t status = 0;
		if(_i2cPort.readByte(TWIST_STATUS, status)){
			val = status & (1 << statusButtonClickedBit);
			
			if(val){
				usleep(10000);  // debounce
				_i2cPort.writeByte(TWIST_STATUS,  status & ~(1 << statusButtonClickedBit));
			}
	}
		
		success = true;
	}
	
	return success;
}

bool QwiicTwist::isMoved(bool& val){
	
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		uint8_t status = 0;
		if(_i2cPort.readByte(TWIST_STATUS, status)){
			val = status & (1 << statusEncoderMovedBit);
			_i2cPort.writeByte(TWIST_STATUS,  status & ~(1 << statusEncoderMovedBit));
		}
		
		success = true;
	}
	
	return success;
}


bool QwiicTwist::timeSinceLastMovement(uint16_t &val, bool clearValue ){
	
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		success = _i2cPort.readWord(TWIST_LAST_ENCODER_EVENT, val);
		
		if (clearValue == true)
			_i2cPort.writeWord(TWIST_LAST_ENCODER_EVENT, 0);
		
	}
	
	return success;
}

bool QwiicTwist::timeSinceLastPress(uint16_t &val, bool clearValue ){
	
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		success = _i2cPort.readWord(TWIST_LAST_BUTTON_EVENT, val);
		
		if (clearValue == true)
			_i2cPort.writeWord(TWIST_LAST_BUTTON_EVENT, 0);
		
	}
	
	return success;
}


bool QwiicTwist::setColor(uint8_t red, uint8_t green, uint8_t blue){
	
	bool success = false;
	
	if(_i2cPort.isAvailable()){
		I2C::i2c_block_t block = {red, green, blue};
		
		success = _i2cPort.writeBlock(TWIST_RED, 3, block);
	}
	
	return success;
}



