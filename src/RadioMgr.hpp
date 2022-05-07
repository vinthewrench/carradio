//
//  RadioMgr.hpp
//  vfdtest
//
//  Created by Vincent Moscaritolo on 5/4/22.
//

#pragma once

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <mutex>
#include <bitset>
#include <sys/time.h>
#include "RtlSdr.hpp"
 
#include "ErrorMgr.hpp"
#include "CommonDefs.hpp"

using namespace std;
 

class RadioMgr {
	
public:
	typedef enum :int {
		RADIO_OFF = 0,
		BROADCAST_AM,
		BROADCAST_FM,
		VHF,
	}radio_mode_t;
	
	typedef enum  {
		MUX_UNKNOWN = 0,
		MUX_MONO,
		MUX_STEREO,
		MUX_QUAD,
	}radio_mux_t;
	
	
	static RadioMgr *shared() {
		if(!sharedInstance){
			sharedInstance = new RadioMgr;
		}
		return sharedInstance;
	}
	
	RadioMgr();
	~RadioMgr();
	
	bool begin(uint32_t deviceIndex  = 0);
	bool begin(uint32_t deviceIndex,  int &error);
	void stop();
	
	
	bool getDeviceInfo(RtlSdr::device_info_t&);
	
	static string freqSuffixString(double hz);
	static string hertz_to_string(double hz, int precision = 1);
	static string modeString(radio_mode_t);
	static string muxstring(radio_mux_t);
	
	radio_mode_t radioMode() {return _mode;};
	bool setRadioMode(radio_mode_t );
	
	radio_mux_t radioMuxMode() {return _mux;};
	
	double frequency();
	bool setFrequency(double );
	
	double nextFrequency(bool up);
	
private:
	bool					_isSetup;
	RtlSdr				_sdr;
	
	radio_mode_t 		_mode;
	double				_frequency;
	radio_mux_t 		_mux;
	 
	static RadioMgr *sharedInstance;
	
};

