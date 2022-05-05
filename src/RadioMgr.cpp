//
//  RadioMgr.cpp
//  vfdtest
//
//  Created by Vincent Moscaritolo on 5/4/22.
//

#include "RadioMgr.hpp"
#include "RtlSdr.hpp"

#include <cmath>

RadioMgr *RadioMgr::sharedInstance = NULL;

RadioMgr::RadioMgr(){
	_mode = RADIO_OFF;
	_mux = MUX_MONO;

 }


RadioMgr::~RadioMgr(){
 }

 

bool RadioMgr::begin(uint32_t deviceIndex){
	int error = 0;

	return begin(deviceIndex, error);
}

bool RadioMgr::begin(uint32_t deviceIndex,  int &error){
	
	_isSetup = false;
	
	if( _sdr.begin(deviceIndex,error )) {
		_isSetup = true;
	}
 
 	return _isSetup;
}

void RadioMgr::stop(){
	
	if(_isSetup  ){
		_sdr.stop();
	}
 	_isSetup = false;
 }



bool RadioMgr::getDeviceInfo(RtlSdr::device_info_t& info){
	return _sdr.getDeviceInfo(info);
}

bool RadioMgr::setRadioMode(radio_mode_t newMode){
	_mode = newMode;
	return true;
}

bool RadioMgr::setFrequency(double newFreq){
	if(newFreq != _frequency){
		_frequency = newFreq;

		// DEBUG
		_mux = (_frequency == 103.5e6)? MUX_STEREO: MUX_MONO;
		//DEBUG

		return true;
	}
	return false;
}
 
string RadioMgr::modeString(radio_mode_t mode){
 
	string str = "   ";
	switch (mode) {
		case BROADCAST_AM:
			str = "AM";
			break;
			
		case BROADCAST_FM:
			str = "FM";
			break;
			
		case VHF:
			str = "VHF";
			break;
			
		default: ;
	}
 
	return str;
}

string RadioMgr::muxstring(radio_mux_t mux){
 
	string str = "  ";
	switch (mux) {
		case MUX_STEREO:
			str = "ST";
			break;
			
		case MUX_QUAD:
			str = "QD";
			break;
			
		default: str = "       ";
	}
 
	return str;
}


string  RadioMgr::freqSuffixString(double hz){
	
	if(hz > 1710e3) { // Mhz
		return "Mhz";
	} else if(hz >= 1.0e3) {
		return "Khz";
	}
	
	return "";
}

double RadioMgr::nextFrequency(bool up){
	
	double newfreq = _frequency;
	
	switch (_mode) {
		case BROADCAST_AM:
			// AM steps are 10khz
			if(up) {
				newfreq+=10.e3;
			}
			else {
				newfreq-=10.e3;
			}
			if(newfreq > 1710e3) newfreq =1710e3;
			else if(newfreq < 530e3) newfreq =530e3;
		break;
	
		case BROADCAST_FM:
			// AM steps are 200khz
			if(up) {
				newfreq+=200.e3;
			}
			else {
				newfreq-=200.e3;
			}
			if(newfreq > 108.1e6) newfreq = 108.1e6;
			else if(newfreq < 88.1e6) newfreq =88.1e6;
		break;

		default:
			if(up) {
				newfreq+=1.e3;
			}
			else {
				newfreq-=1.e3;
			}
			break;
	}
	return newfreq;
}

string  RadioMgr::hertz_to_string(double hz, int precision){
	
	char buffer[128] = {0};
 
	if(hz > 1710e3) { // Mhz
		sprintf(buffer, "%*.*f", precision+4, precision, hz/1.0e6);
	} else if(hz >= 1.0e3) {
		sprintf(buffer, "%4d", (int)round( hz/1.0e3));
	}
	
	return string(buffer);
}
