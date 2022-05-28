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

	status = _sensor.begin(deviceAddress, error);
	
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
	_state = INS_INVALID;
}

bool CompassSensor::isConnected(){
	return   (_state != INS_INVALID) && (_state != INS_UNKNOWN);
	
}
 
void CompassSensor::reset(){
	_sensor.reset();
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
					
	//				if(diff.tv_sec >=  _queryDelay  ) {
				if(diff.tv_usec >=  5e5  ) {
						shouldQuery = true;
					}
				}
				
				if(shouldQuery){
					
//					_sensor.startTempMeasurement();
//					_state = INS_WAITING_FOR_TEMP;
//
//				}
//			}
//				break;
//
//			case INS_WAITING_FOR_TEMP:
//			{
//				if( _sensor.isTempMeasurementDone()) {
//					float tempC;
//
//					if( _sensor.readTempC(tempC)) {
//						_resultMap[VAL_COMPASS_TEMP] =  to_string(tempC);
						
						_sensor.startMagMeasurement();
						_state = INS_WAITING_FOR_MAG;
						//					gettimeofday(&_lastQueryTime, NULL);
					}
	//			}
			}
				break;
				
			case INS_WAITING_FOR_MAG:
			{
				if( _sensor.isMagMeasurementDone()) {
					
					if( _sensor.readMag()) {
						//				_resultMap[VAL_COMPASS_TEMP] =  to_string(tempC);
						
						_state = INS_RESPONSE;
						gettimeofday(&_lastQueryTime, NULL);
					}
					
				}
			}
				break;
				
				
			default:;
		}
		
	}
}
