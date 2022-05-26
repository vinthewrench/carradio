//
//  PiCarCAN.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/25/22.
//

#pragma once


#include "CommonDefs.hpp"

#include <time.h>

#include "CANBusMgr.hpp"

#include "GMLAN.hpp"
#include "OBD2.hpp"
#include "Wranger2010.hpp"

using namespace std;


class PiCarCAN {
	
public:

	PiCarCAN();
	~PiCarCAN();
		
	bool begin();
	bool begin(int &error);
	void stop();
	bool reset();

	bool lastFrameTime(time_t &time);
	bool packetCount(size_t &count);
	bool resetPacketCount();
 
private:
	bool 				_isSetup = false;

	CANBusMgr		_CANbus;
	Wranger2010		_jeep;
	GMLAN 			_gmlan;
	OBD2				_obdii;

};

