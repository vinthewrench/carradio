//
//  DuppaLEDRing.hpp
//  Duppatest
//
//  Created by Vincent Moscaritolo on 5/14/22.
//
 
#pragma once

#include "I2C.hpp"
#include "RGB.hpp"

using namespace std;

class DuppaLEDRing
{
 
public:
 
	typedef struct {
		uint8_t 	r;
		uint8_t	g;
		uint8_t	b;
	} led_color_t;
		
	typedef led_color_t led_block_t[24];
 
	DuppaLEDRing();
	~DuppaLEDRing();
 
	bool begin(uint8_t deviceAddress);
	bool begin(uint8_t deviceAddress, int &error);
	void stop();

	void setOffset(uint8_t offset, bool flip = false){
		_ledOffset = offset;
		_flipOffset = flip;
	};
	
	bool reset();
 
	bool clearAll();
	
	bool setColor(uint8_t led_n, led_color_t color);
	bool setLEDs(led_block_t &leds);

	bool setColor(uint8_t led_n, uint8_t red, uint8_t green, uint8_t blue );
	bool setRED(uint8_t led_n, uint8_t color);
	bool setGREEN(uint8_t led_n, uint8_t color);
	bool setBLUE(uint8_t led_n, uint8_t color);
	bool setColor(uint8_t led_n, RGB color);

	uint8_t	getDevAddr();
 
	bool  setConfig(uint8_t b);
	bool	PWM_MODE(void) ;
	bool 	PWMFrequencyEnable(uint8_t PWMenable);
	bool  SpreadSpectrum(uint8_t spread);
	bool  SetScaling(uint8_t scal);
	bool  GlobalCurrent(uint8_t curr);
	
private:
 
	bool  selectBank(uint8_t b);
	
	uint8_t ledFromOffset(uint8_t led_n);
	
	uint8_t 	_ledOffset;
	bool 		_flipOffset;
	
	const uint8_t issi_led_map[3][24] = {
	  {0x48, 0x36, 0x24, 0x12, 0x45, 0x33, 0x21, 0x0F, 0x42, 0x30, 0x1E, 0x0C, 0x3F, 0x2D, 0x1B, 0x09, 0x3C, 0x2A, 0x18, 0x06, 0x39, 0x27, 0x15, 0x03}, // Red
	  {0x47, 0x35, 0x23, 0x11, 0x44, 0x32, 0x20, 0x0E, 0x41, 0x2F, 0x1D, 0x0B, 0x3E, 0x2C, 0x1A, 0x08, 0x3B, 0x29, 0x17, 0x05, 0x38, 0x26, 0x14, 0x02}, //Green
	  {0x46, 0x34, 0x22, 0x10, 0x43, 0x31, 0x1F, 0x0D, 0x40, 0x2E, 0x1C, 0x0A, 0x3D, 0x2B, 0x19, 0x07, 0x3A, 0x28, 0x16, 0x04, 0x37, 0x25, 0x13, 0x01}, //Blue
	};

	
	I2C 		_i2cPort;
	bool		_isSetup;

 
};
 
