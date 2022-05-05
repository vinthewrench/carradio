//
//  QwiicTwist.hpp
//  vfdtest
//
//  Created by Vincent Moscaritolo on 4/13/22.
//

#pragma once

#include "I2C.hpp"

using namespace std;

class QwiicTwist
{
 
public:
	QwiicTwist();
	~QwiicTwist();
 
 
	bool begin(uint8_t deviceAddress = 0x3F);
	bool begin(uint8_t deviceAddress,  int &error);
	void stop();
 
	uint8_t	getDevAddr();
 
	bool getCount(int16_t&);
	bool setCount(int16_t);

	bool getLimit(uint16_t&);
	bool setLimit(uint16_t);

	bool getDiff(int16_t&, bool clearValue );
	
	bool isPressed(bool&);
	bool isClicked(bool&);
	bool isMoved(bool&);

	bool timeSinceLastMovement(uint16_t&, bool clearValue );
	bool timeSinceLastPress(uint16_t&, bool clearValue );
	
	bool setColor(uint8_t red, uint8_t green, uint8_t blue);

private:
 
 
	
	I2C 		_i2cPort;
	bool		_isSetup;

};
 
