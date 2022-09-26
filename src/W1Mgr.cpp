//
//  W1Mgr.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 7/28/22.
//

#include "W1Mgr.hpp"
#include "PiCarMgr.hpp"

#include <fcntl.h>
#include <cassert>
#include <string.h>

#include <stdlib.h>
#include <errno.h> // Error integer and strerror() function

#include <iostream>
#include <filesystem> // C++17
#include <fstream>

#include "ErrorMgr.hpp"
#include "timespec_util.h"


typedef void * (*THREADFUNCPTR)(void *);

W1Mgr::W1Mgr() {
	_isSetup = false;
	_queryDelay 		= 5;	// seconds
	_lastQueryTime 	= {0,0};
	_temperatureData.clear();
	_isRunning = true;

	pthread_create(&_TID, NULL,
										  (THREADFUNCPTR) &W1Mgr::W1ReaderThread, (void*)this);

	
}


W1Mgr::~W1Mgr(){
	stop();
	
	pthread_mutex_lock (&_mutex);
	_isRunning = false;
	_temperatureData.clear();

	pthread_cond_signal(&_cond);
	pthread_mutex_unlock (&_mutex);

	pthread_join(_TID, NULL);
 
}


bool W1Mgr::begin(uint64_t queryDelay){
	int error = 0;
	
	return begin(queryDelay, error);
}


bool W1Mgr::begin(uint64_t queryDelay,  int &error){

	if(isConnected())
		return true;
 
	_queryDelay = queryDelay;
	_lastQueryTime 	= {0,0};
	_temperatureData.clear();
	_shouldRead = true;
 	_isSetup = true;
	

	return _isSetup;
}

void W1Mgr::stop(){
	
	if(_isSetup) {
		_shouldRead = false;
		_isSetup = false;
		_temperatureData.clear();
	}
}

bool  W1Mgr::isConnected() {
	return _isSetup;
};

void W1Mgr::setQueryDelay(uint64_t delay){
	_queryDelay = delay;
	_lastQueryTime = {0,0};
};


bool W1Mgr::setShouldRead(bool shouldRead){
	if(_isSetup && _isRunning){
		_shouldRead = shouldRead;
		return true;
	}
	return false;
}

bool 	W1Mgr::getTemperatures( map<string,float> & temps){
	bool success = false;
	
	if(_isSetup) {
		pthread_mutex_lock (&_mutex);
		temps = _temperatureData;
		success = true;
		pthread_mutex_unlock (&_mutex);
		
	}
	return success;

}

stringvector	W1Mgr::getW1_slaveInfo(string deviceID){
	stringvector lines ={};
	std::ifstream	ifs;

	string path = "/sys/bus/w1/devices/" + deviceID + "/w1_slave";

	try{
		// create a list of devices to check
		string line;

		ifs.open(path, ios::in);
		if( ifs.is_open()){
	 
			while ( std::getline(ifs, line) ) {
				line = Utils::trim(line);
				lines.push_back(line);
			}
			ifs.close();
		}

	}
	catch(std::ifstream::failure &err) {
		ELOG_MESSAGE("reading from w1_master_slaves:FAIL: %s", err.what());
	}
	return lines;
}

stringvector W1Mgr::getDeviceIDs(){
	
	// Get a list of 1 wire devices
	
	static const char* w1_devices_filepath  = "/sys/bus/w1/drivers/w1_master_driver/w1_bus_master1/w1_master_slaves";
	
	std::ifstream	ifs;
	stringvector deviceIds;
	deviceIds .clear();
	
	try{
		// create a list of devices to check
		string line;

		ifs.open(w1_devices_filepath, ios::in);
		if( ifs.is_open()){
			deviceIds.clear();
			
			while ( std::getline(ifs, line) ) {
				line = Utils::trim(line);
				deviceIds.push_back(line);
			}
			ifs.close();
		}

	}
	catch(std::ifstream::failure &err) {
		ELOG_MESSAGE("reading from w1_master_slaves:FAIL: %s", err.what());
	}
	return deviceIds;
}


bool W1Mgr::processDS18B20(string deviceName){
	
	bool success = false;
	std::ifstream	ifs;

	string path = "/sys/bus/w1/devices/" + deviceName + "/temperature";

	try{
		// create a list of devices to check
		string line;
		
		ifs.open(path, ios::in);
		if( ifs.is_open()
			&& std::getline(ifs, line) ){
			line = Utils::trim(line);
			if(!line.empty()){
				float tempC = stof(line);
				tempC = tempC / 1000.;	// scale temp
				pthread_mutex_lock (&_mutex);
				_temperatureData[deviceName] = tempC;
				pthread_mutex_unlock (&_mutex);
				success = true;
				
				}
		}
		ifs.close();
	}
 	catch(std::ifstream::failure &err) {
		ELOG_MESSAGE("reading from temperature:FAIL: %s", err.what());
	}
	
	return success;
}

void W1Mgr::W1Reader(){
	
	PRINT_CLASS_TID;

	while(_isRunning){
		// if not setup // check back later
		if(!_isSetup){
			sleep(2);
			continue;
		}
		
		if(!_shouldRead ){
			usleep(500000);
			continue;
		}
		
		bool shouldQuery = false;
		
		if(_lastQueryTime.tv_sec == 0 &&  _lastQueryTime.tv_nsec == 0 ){
			shouldQuery = true;
		} else {
			
			struct timespec now, diff;
			clock_gettime(CLOCK_MONOTONIC, &now);
			diff = timespec_sub(now, _lastQueryTime);
 
			if(diff.tv_sec >=  _queryDelay  ) {
				shouldQuery = true;
			}
		}
		
		if(shouldQuery){
 
			// create an updated list of deviceIDs
			stringvector deviceIds = getDeviceIDs();
			
			for(string deviceName: deviceIds){
				
				vector<string> v = split<string>(deviceName, "-");
				if(v.size() != 2) break;
				
				//	The modules create a subdirectory for each sensor found just below /sys/bus/w1/devices.
				// The directory name is composed of the Family Code of the sensor and its unique identification number.
				//Sensors of the type DS1820 and DS18S20 have the Family Code 10,
				// DS18B20 has Code 28 and DS1822 the 22.
				// In each subdirectory there is the file w1_slave containing the sensor status and
				// measured temperature value:
				
				// process the sensors
				if(v[0] == "28") {
					processDS18B20(deviceName);
				}
				
			}
			
			clock_gettime(CLOCK_MONOTONIC, &_lastQueryTime);
		}
		else
		{
			usleep(500000);
		}
	}
}


void* W1Mgr::W1ReaderThread(void *context){
	W1Mgr* d = (W1Mgr*)context;

	//   the pthread_cleanup_push needs to be balanced with pthread_cleanup_pop
	pthread_cleanup_push(   &W1Mgr::W1ReaderThreadCleanup ,context);
 
	d->W1Reader();
	
	pthread_exit(NULL);
	
	pthread_cleanup_pop(0);
	return((void *)1);
}

 
void W1Mgr::W1ReaderThreadCleanup(void *context){
 
	printf("cleanup W1Mgr\n");
}
 
