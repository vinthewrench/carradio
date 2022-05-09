//
//  QTRotaryKnob.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/9/22.
//

#pragma once

#include "RotaryKnob.hpp"
#include "QwiicTwist.hpp"
#include <stdlib.h>

using namespace std;
 
class QTKnob : public RotaryKnob{
 
public:

	QTKnob();
	~QTKnob();

	bool begin(int deviceAddress);
	bool begin(int deviceAddress,  int &error);
	void stop();
 
	bool isConnected();
  
	bool wasClicked();
	bool wasMoved( bool &up);

private:
	
	bool				_isSetup;
 	QwiicTwist		_twist;
	
	int16_t 			_twistCount;

};
