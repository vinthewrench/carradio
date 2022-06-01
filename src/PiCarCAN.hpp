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

	typedef enum  {
		CAN_ALL= 0,
		CAN_GM,
		CAN_JEEP,
	}pican_bus_t;

	PiCarCAN();
	~PiCarCAN();
		
	bool begin();
	bool begin(int &error);
	void stop();
	bool reset();

	bool lastFrameTime(pican_bus_t bus, time_t &time);
	bool packetCount(pican_bus_t bus, size_t &count);
	bool resetPacketCount(pican_bus_t bus);
 
	bool getStatus(vector<CANBusMgr::can_status_t> & stats);
 
	FrameDB* frameDB() {return  _CANbus.frameDB();};

	// ODB request need to be polled.. this starts and stops the polling
	
	bool request_ODBpolling(string key);
	bool cancel_ODBpolling(string key);

	
private:
	bool 				_isSetup = false;

	CANBusMgr		_CANbus;
	Wranger2010		_jeep;
	GMLAN 			_gmlan;
	OBD2				_obdii;

};

