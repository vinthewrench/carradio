//
//  CompassSensor.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/27/22.
//

#include "CompassSensor.hpp"
#include "PropValKeys.hpp"
#include "ErrorMgr.hpp"

 
CompassSensor::CompassSensor(){
	_state = INS_UNKNOWN;
	_lastQueryTime = {0,0};
	_resultMap.clear();
}

CompassSensor::~CompassSensor(){
	stop();
}


bool CompassSensor::begin(int deviceAddress){
	int error = 0;

	return begin(deviceAddress, error);
}

 
bool CompassSensor::begin(int deviceAddress, int &error){
	bool status = false;

	if(deviceAddress == 0)
		deviceAddress = BNO055_I2C_ADDR1;
	
 	status = _compass.begin(deviceAddress, error);
	
	if(status){
		_state = INS_IDLE;
		_queryDelay = 1;	// seconds
		_lastQueryTime = {0,0};
		_resultMap.clear();
		reset();

	}else {
		_state = INS_INVALID;
	}
	

	return status;
}

void CompassSensor::stop(){
	
	_compass.stop();
	_state = INS_INVALID;
}

bool CompassSensor::isConnected(){
	return   (_state != INS_INVALID) && (_state != INS_UNKNOWN);
	
}
 

bool CompassSensor::versionString(string & version){
	
	BNO055_Compass::BNO055_info_t info;
	
	if(isConnected() && _compass.getInfo(info)){
		string str = "BNO055 " + to_string( info.sw_rev_id);
		version = str;
		return true;
	}
	
	return false;
}

void CompassSensor::reset(){
//	_sensor.reset();
}

void CompassSensor::setQueryDelay(uint64_t delay){
	_queryDelay = delay;
	_lastQueryTime = {0,0};
};


PiCarMgrDevice::response_result_t
CompassSensor::rcvResponse(std::function<void(map<string,string>)> cb){

	PiCarMgrDevice::response_result_t result = NOTHING;
	
	if(!isConnected()) {
		return ERROR;
	}
	
	if(_state == INS_RESPONSE){
		result = PROCESS_VALUES;
		if(cb) (cb)(_resultMap);
		_state = INS_IDLE;
	}
 
	if(result == CONTINUE)
		return result;

	if(result ==  INVALID){
//		uint8_t sav =  ErrorMgr::shared()->_logFlags;
//		START_VERBOSE;
//		ErrorMgr::shared()->logTimedStampString("CompassSensor INVALID: ");
//		ErrorMgr::shared()->_logFlags = sav;
		return result;
	}
	
	return result;
}

 

PiCarMgrDevice::device_state_t CompassSensor::getDeviceState(){
  
  device_state_t retval = DEVICE_STATE_UNKNOWN;
  
  if(!isConnected())
	  retval = DEVICE_STATE_DISCONNECTED;
  
  else if(_state == INS_INVALID)
	  retval = DEVICE_STATE_ERROR;
  
  else retval = DEVICE_STATE_CONNECTED;

  return retval;
}

 

void CompassSensor::idle(){
	
	
	if(isConnected()){
		switch (_state) {
			case INS_IDLE: {
				
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
					
					
				}
			}
				break;
				
				
			default:;
		}
		
	}
}
