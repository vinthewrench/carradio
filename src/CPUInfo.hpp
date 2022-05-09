//
//  CPUInfo.hpp
//  coopMgr
//
//  Created by Vincent Moscaritolo on 9/14/21.
//

#pragma once

#include <stdio.h>
#include <functional>
#include <string>
#include <sys/time.h>

#include <map>
#include "PiCanMgrDevice.hpp"

using namespace std;

 
class CPUInfo : public PiCanMgrDevice{
 
public:
	inline static const string CPU_INFO_TEMP = "CPU_TEMP";
	inline static const string PROP_CPU_TEMP_QUERY_DELAY = "cputemp-query-delay";

	CPUInfo();
	~CPUInfo();

	bool begin();
	bool begin(int &error);
	void stop();

	bool isConnected();
 
	response_result_t rcvResponse(std::function<void(map<string,string>)> callback = NULL);
	
	void idle(); 	// called from loop
	void reset(); 	// reset from timeout
	void setQueryDelay(uint64_t);

	device_state_t getDeviceState();
	
private:

	typedef enum  {
		INS_UNKNOWN = 0,
		INS_IDLE ,
		INS_INVALID,
		INS_RESPONSE,
	 
	}in_state_t;
	
	in_state_t 			_state;
	map<string,string> _resultMap;
 
	timeval			_lastQueryTime;
	uint64_t     	_queryDelay;			// how long to wait before next query

	
	bool getCPUTemp(double* tempOut);

};
 
