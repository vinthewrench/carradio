//
//  TempSensor.cpp
//  coopMgr
//
//  Created by Vincent Moscaritolo on 9/11/21.
//

#include "TempSensor.hpp"
#include "ErrorMgr.hpp"

TempSensor::TempSensor(){
	_state = INS_UNKNOWN;
	_lastQueryTime = {0,0};
	_resultMap.clear();
}

TempSensor::~TempSensor(){
	stop();
}


bool TempSensor::begin(int deviceAddress, string resultKey){
	int error = 0;

	return begin(deviceAddress, resultKey, error);
}

 
bool TempSensor::begin(int deviceAddress, string resultKey, int &error){
	bool status = false;

	status = _sensor.begin(deviceAddress, error);
	
	if(status){
		_state = INS_IDLE;
		_queryDelay = 2;	// seconds
		_lastQueryTime = {0,0};
		_resultMap.clear();
		_resultKey = resultKey;

	}else {
		_state = INS_INVALID;
	}
	

	return status;
}

void TempSensor::stop(){
	_sensor.stop();
	_state = INS_INVALID;
}

bool TempSensor::isConnected(){
	return  (_state ==  INS_IDLE || _state ==  INS_RESPONSE);
	
}
 
void TempSensor::reset(){
	
}

void TempSensor::setQueryDelay(uint64_t delay){
	_queryDelay = delay;
	_lastQueryTime = {0,0};
};


PiCarMgrDevice::response_result_t
TempSensor::rcvResponse(std::function<void(map<string,string>)> cb){

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
//		ErrorMgr::shared()->logTimedStampString("TempSensor INVALID: ");
//		ErrorMgr::shared()->_logFlags = sav;
		return result;
	}
	
	return result;
}

 

PiCarMgrDevice::device_state_t TempSensor::getDeviceState(){
  
  device_state_t retval = DEVICE_STATE_UNKNOWN;
  
  if(!isConnected())
	  retval = DEVICE_STATE_DISCONNECTED;
  
  else if(_state == INS_INVALID)
	  retval = DEVICE_STATE_ERROR;
  
  else retval = DEVICE_STATE_CONNECTED;

  return retval;
}

bool TempSensor::tempC(float& tempC){
	return _sensor.readTempC(tempC);
}


void TempSensor::idle(){
	
	if(isConnected() && (_state == INS_IDLE)){
		
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
			
			float tempC;
			
			if( _sensor.readTempC(tempC)){
				_resultMap[_resultKey] =  to_string(tempC);
				_state = INS_RESPONSE;
				gettimeofday(&_lastQueryTime, NULL);
				
			}
			else
			{
//				_state = INS_INVALID;
			}
			
		}
	}
}
