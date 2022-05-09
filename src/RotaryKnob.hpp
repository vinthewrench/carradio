//
//  RotaryKnob.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/9/22.
//

 
#pragma once
#include <stdbool.h>

 
using namespace std;

class RotaryKnob  {
 
public:

//	bool begin(int deviceAddress);
//	bool begin(int deviceAddress,  int &error);
	
	virtual bool wasClicked() = 0;
	virtual bool wasMoved( bool &up) = 0;
	
	virtual void stop() = 0;
	virtual bool isConnected()  = 0;
	 
private:
};
 
