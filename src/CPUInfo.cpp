//
//  CPUInfo.cpp
//  coopMgr
//
//  Created by Vincent Moscaritolo on 9/14/21.
//

#include "CPUInfo.hpp"
#include "ErrorMgr.hpp"
#include <iostream>
#include <filesystem> // C++17
#include <fstream>
#include "PropValKeys.hpp"
 
CPUInfo::CPUInfo(){
	_state = INS_UNKNOWN;
	_lastQueryTime = {0,0};
	_resultMap.clear();
}

CPUInfo::~CPUInfo(){
	stop();
}

bool CPUInfo::begin(){
	int error = 0;

	return begin(error);
}

 
bool CPUInfo::begin(int &error){
 
	_state = INS_IDLE;
	_queryDelay = 2;	// seconds
	_lastQueryTime = {0,0};
 
	return true;
}

void CPUInfo::stop(){
	_state = INS_INVALID;
}

bool CPUInfo::isConnected(){
	return _state == INS_IDLE  ||  _state == INS_RESPONSE;
}
 
void CPUInfo::reset(){
	
}

void CPUInfo::setQueryDelay(uint64_t delay){
	_queryDelay = delay;
	_lastQueryTime = {0,0};
};

PiCarMgrDevice::response_result_t
CPUInfo::rcvResponse(std::function<void(map<string,string>)> cb){

	PiCarMgrDevice::response_result_t result = NOTHING;
	
	if(_state == INS_RESPONSE){
		result = PROCESS_VALUES;
		if(cb) (cb)(_resultMap);
		_state = INS_IDLE;
	}
 
	if(result == CONTINUE)
		return result;

	if(result ==  INVALID){
//		uint8_t sav =  LogMgr::shared()->_logFlags;
//		START_VERBOSE;
//		LogMgr::shared()->logTimedStampString("CPUInfo INVALID: ");
//		LogMgr::shared()->_logFlags = sav;
		return result;
	}
	return result;
}

 

PiCarMgrDevice::device_state_t CPUInfo::getDeviceState(){
  
  device_state_t retval = DEVICE_STATE_UNKNOWN;
  
  if(!isConnected())
	  retval = DEVICE_STATE_DISCONNECTED;
  
  else if(_state == INS_INVALID)
	  retval = DEVICE_STATE_ERROR;
  
  else retval = DEVICE_STATE_CONNECTED;

  return retval;
}

void CPUInfo::idle(){
	
	
	if(_state == INS_IDLE){
		
		bool shouldQuery = false;
		
		if(_lastQueryTime.tv_sec == 0 &&  _lastQueryTime.tv_usec == 0 ){
			shouldQuery = true;
		} else {
			
			timeval now, diff;
			gettimeofday(&now, NULL);
			timersub(&now, &_lastQueryTime, &diff);
			
			if(diff.tv_sec >=  _queryDelay  ) {
				shouldQuery = true;
			}
		}
		
		if(shouldQuery){
			
			double tempC;
			
			if(getCPUTemp(&tempC)){
				_resultMap[VAL_CPU_INFO_TEMP] =  to_string(tempC);
				_state = INS_RESPONSE;
				gettimeofday(&_lastQueryTime, NULL);
				
			}
		}
	}
}

bool CPUInfo::getCPUTemp(double * tempOut) {
	bool didSucceed = false;
	
//	/// DEBUG
//
//	if(tempOut){
//		string val = "4000";
// 		double temp = std::stod(val);
//		temp = temp /1000.0;
//		*tempOut = temp;
//	}
//	didSucceed = true;
//	return didSucceed;
//
//
//	// DEBUG
	
	try{
#if defined(__APPLE__)
		*tempOut = 48.199;
		didSucceed = true;
#else
		
		std::ifstream   ifs;
		ifs.open("/sys/class/thermal/thermal_zone0/temp", ios::in);
		if( ifs.is_open()){
			
			if(tempOut){
				string val;
				ifs >> val;
				ifs.close();
				double temp = std::stod(val);
				temp = temp /1000.0;
				*tempOut = temp;
			}
			didSucceed = true;
		}
#endif
		
	}
	catch(std::ifstream::failure &err) {
	}
	
	
	return didSucceed;
}
