//
//  RadioDataSource.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/5/22.
//

#include "RadioDataSource.hpp"
#include <stdexcept>
#include <sstream>
#include <ctime>
#include <cmath>
#include <iomanip>
#include <time.h>
#include <sys/time.h>
#include <iostream>
#include <filesystem> // C++17
#include <fstream>
#include "AudioOutput.hpp"

bool getCPUTemp(float & tempOut) {
	bool didSucceed = false;
	
	try{
		std::ifstream   ifs;
		ifs.open("/sys/class/thermal/thermal_zone0/temp", ios::in);
		if( ifs.is_open()){
			
			if(tempOut){
				string val;
				ifs >> val;
				ifs.close();
				float temp = std::stof(val);
				temp = temp /1000.0;
				tempOut = temp;
			}
			didSucceed = true;
		}
		
	}
	catch(std::ifstream::failure &err) {
	}
	
	
	return didSucceed;
}


RadioDataSource::RadioDataSource( TMP117* temp, QwiicTwist* vol){
	_vol = vol;
	_tmp117 = temp;
}

bool RadioDataSource::getStringForKey(string_view key,  string &result){
 
	return false;
}

bool RadioDataSource::getIntForKey(string_view key,  int &result){
	
	if(key == DS_KEY_MODULATION_MODE){
	 
		RadioMgr*	radio = RadioMgr::shared();
		result = radio->radioMode();
		return  true;
	}
	else if(key == DS_KEY_MODULATION_MUX){
	 
		RadioMgr*	radio = RadioMgr::shared();
		result = radio->radioMuxMode();
		return  true;
	}
 
	return false;
}

 
bool RadioDataSource::getDoubleForKey(string_view key,  double &result){
	
	if(key == DS_KEY_RADIO_FREQ){
		RadioMgr*	radio = RadioMgr::shared();
		result = radio->frequency();
		return  true;
		 
	}
 
	return false;
}

bool RadioDataSource::getFloatForKey(string_view key,  float &result){
	
	if(key == DS_KEY_OUTSIDE_TEMP){
		float temp = 0;
		if(_tmp117->readTempF(temp)) {
			result = temp;
			return  true;
		}
	}
	else if(key == DS_KEY_RADIO_VOLUME){
		
		AudioOutput* 	audio 	= AudioOutput::shared();
		result = audio->volume() ;
		return true;
	}
	else if(key == DS_KEY_CPU_TEMP){
		float temp = 0;
		if(getCPUTemp(temp)) {
			result = temp;
			return  true;
		}
	}
	
	return false;
}
