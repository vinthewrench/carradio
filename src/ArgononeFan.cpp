//
//  ArgononeFan.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 7/5/22.
//

#include "ArgononeFan.hpp"
#include "ErrorMgr.hpp"
#include <iostream>
#include <filesystem> // C++17
#include <fstream>
#include "PropValKeys.hpp"
#include "ErrorMgr.hpp"

#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <unistd.h>
#include <sys/types.h>


ArgononeFan::ArgononeFan(){
	_state = INS_UNKNOWN;
	_lastQueryTime = {0,0};
	_resultMap.clear();
	_shm_fd = -1;
	_shm_ptr = NULL;
}

ArgononeFan::~ArgononeFan(){
	stop();
}

bool ArgononeFan::begin(){
	int error = 0;

	return begin(error);
}

 
bool ArgononeFan::begin(int &error){
 
	_shm_fd =  shm_open(SHM_FILE, O_RDWR, 0664);
	if(_shm_fd == -1){
		ELOG_ERROR(ErrorMgr::FAC_DEVICE, 0, 0,  "FAN Shared memory open failed %s",SHM_FILE);
		_state = INS_UNKNOWN;
		return false;
	}
	
	ftruncate(_shm_fd, SHM_SIZE);
	_shm_ptr = (struct SHM_Data*) mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, _shm_fd, 0);
	 if (_shm_ptr == MAP_FAILED) {
		 
		 ELOG_ERROR(ErrorMgr::FAC_DEVICE, 0, 0,  "FAN Shared memory map error %s",SHM_FILE);
		 _state = INS_UNKNOWN;
		 close(_shm_fd);
		 _shm_fd = -1;
		 _shm_ptr = NULL;
		 return false;

 	}
	_queryDelay = 2;	// seconds
	_lastQueryTime = {0,0};

	_state = INS_IDLE;
	return true;
}

void ArgononeFan::stop(){
	_state = INS_INVALID;
	
	if(_shm_fd > -1){
		close(_shm_fd);
		_shm_fd = -1;
		_shm_ptr = NULL;
	}
}

bool ArgononeFan::isConnected(){
	return _state == INS_IDLE  ||  _state == INS_RESPONSE;
}
 
void ArgononeFan::reset(){
	
}

void ArgononeFan::setQueryDelay(uint64_t delay){
	_queryDelay = delay;
	_lastQueryTime = {0,0};
};

PiCarMgrDevice::response_result_t
ArgononeFan::rcvResponse(std::function<void(map<string,string>)> cb){

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

 

PiCarMgrDevice::device_state_t ArgononeFan::getDeviceState(){
  
  device_state_t retval = DEVICE_STATE_UNKNOWN;
  
  if(!isConnected())
	  retval = DEVICE_STATE_DISCONNECTED;
  
  else if(_state == INS_INVALID)
	  retval = DEVICE_STATE_ERROR;
  
  else retval = DEVICE_STATE_CONNECTED;

  return retval;
}

void ArgononeFan::idle(){
	
	
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
			
			uint8_t fanSpeed;
			
			if(getFanSpeed(&fanSpeed)){
				_resultMap[VAL_FAN_SPEED] = fanSpeed;
				_state = INS_RESPONSE;
				gettimeofday(&_lastQueryTime, NULL);
				
			}
		}
	}
}

bool ArgononeFan::getFanSpeed(uint8_t* fanSpeed){
	bool didSucceed = false;
 
	if(_shm_ptr ){
 		*fanSpeed = _shm_ptr->fanspeed;
 		didSucceed = true;
	}
	
	return didSucceed;
}
