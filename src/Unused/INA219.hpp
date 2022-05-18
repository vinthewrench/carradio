//
//  INA219.hpp

// this is a modified version of the Adafruit INA219 library
//  https://github.com/adafruit/Adafruit_INA219
// made to work on Spasrkfun Artemis
//

/*!
 * @file Adafruit_INA219.h
 *
 * This is a library for the Adafruit INA219 breakout board
 * ----> https://www.adafruit.com/product/904
 *
 * Adafruit invests time and resources providing this open source code,
 * please support Adafruit and open-source hardware by purchasing
 * products from Adafruit!
 *
 * Written by Bryan Siepert and Kevin "KTOWN" Townsend for Adafruit Industries.
 *
 * BSD license, all text here must be included in any redistribution.
 *
 */

#pragma once

#include "I2C.hpp"

using namespace std;

class INA219
{
 
public:
	INA219();
	~INA219();
 
	// Address of Temperature sensor (0x48,0x49,0x4A,0x4B)
 
	bool begin(uint8_t deviceAddress = 0x40);
	bool begin(uint8_t deviceAddress,  int &error);
	void stop();
 
 	uint8_t	getDevAddr();

	void setCalibration_32V_2A();
	void setCalibration_32V_1A();
	void setCalibration_16V_400mA();
	
	float getBusVoltage_V();
	float getShuntVoltage_mV();
	float getCurrent_mA();
	float getPower_mW();
	
	void powerSave(bool on);

private:
 
	uint32_t 	_calValue;

	// The following multipliers are used to convert raw current and power
	// values to mA and mW, taking into account the current config settings
	uint32_t 	_currentDivider_mA;
	float 		_powerMultiplier_mW;

	int16_t getBusVoltage_raw();
	int16_t getShuntVoltage_raw();
	int16_t getCurrent_raw();
	int16_t getPower_raw();
	

	
	I2C 		_i2cPort;
	bool		_isSetup;

};
 
