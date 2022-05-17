//
//  DuppaKnob.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/17/22.
//

#pragma once

#include "RotaryKnob.hpp"
#include "DuppaEncoder.hpp"
#include <stdlib.h>

using namespace std;
 
class DuppaKnob : public RotaryKnob{
 
public:

	DuppaKnob();
	~DuppaKnob();

	bool begin(int deviceAddress);
	bool begin(int deviceAddress,  int &error);
	void stop();
 
	bool isConnected();
  
	
	bool wasClicked();
	bool wasMoved( bool &cw);

	bool updateStatus();
	bool updateStatus(uint8_t &regOut);
	bool setColor(uint8_t red, uint8_t green, uint8_t blue );

private:
	
	bool					_isSetup;
	DuppaEncoder		_duppa;
	
	int16_t 				_twistCount;

};
