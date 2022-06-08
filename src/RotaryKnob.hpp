//
//  RotaryKnob.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/9/22.
//

 
#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "RGB.hpp"
  
class RotaryKnob  {
 
public:

//	bool begin(int deviceAddress);
//	bool begin(int deviceAddress,  int &error);
	
	virtual bool wasClicked() = 0;
	virtual bool wasDoubleClicked() = 0;
	virtual bool wasMoved( bool &up) = 0;
	
	virtual void stop() = 0;
	virtual bool isConnected()  = 0;
	 
private:
};
 
