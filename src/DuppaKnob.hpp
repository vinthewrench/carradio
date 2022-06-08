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
	bool wasDoubleClicked();
	bool wasMoved( bool &cw);

	bool updateStatus();
	bool updateStatus(uint8_t &regOut);
	bool setColor(uint8_t red, uint8_t green, uint8_t blue );

	bool setColor(RGB color);

	
	bool setAntiBounce(uint8_t period); // period * 0.192ms
	bool setDoubleClickTime(uint8_t period);  // period * 10ms

	
private:
	
	bool					_isSetup;
	DuppaEncoder		_duppa;
};
