//
//  CompassSensor.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/27/22.
//

#pragma once

#include <stdio.h>
#include <functional>
#include <string>
#include <sys/time.h>

#include <map>
#include "PiCarMgrDevice.hpp"
#include "BNO055_Compass.hpp"

using namespace std;

class CompassSensor : public PiCarMgrDevice{
 
public:

	CompassSensor();
	~CompassSensor();

	bool begin(int deviceAddress);
	bool begin(int deviceAddress,  int &error);
	void stop();
	
	void setQueryDelay(uint64_t);

	bool isConnected();
	bool versionString(string & version);

	
	response_result_t rcvResponse(std::function<void(map<string,string>)> callback = NULL);
	
	void idle(); 	// called from loop
	void reset(); 	// reset from timeout

	device_state_t getDeviceState();
	
 
private:

	typedef enum  {
		INS_UNKNOWN = 0,
		INS_IDLE ,
		INS_INVALID,
		INS_RESPONSE,
		
	  
	}in_state_t;

	
	in_state_t 		_state;
	map<string,string> _resultMap;
 
 
	timeval			_lastQueryTime;
	uint64_t     	_queryDelay;			// how long to wait before next query

	BNO055_Compass	_compass;
};
 
