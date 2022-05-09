//
//  PiCanMgr.h
//  carradio
//
//  Created by Vincent Moscaritolo on 5/8/22.
//


#pragma once
 

#include <stdio.h>
#include <vector>
#include <map>
#include <algorithm>
#include <mutex>
#include <bitset>
#include <strings.h>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <functional>
#include <cstdlib>
#include <signal.h>



#include "CommonDefs.hpp"
#include "PiCanMgrDevice.hpp"
#include "DisplayMgr.hpp"
#include "AudioOutput.hpp"
#include "RadioMgr.hpp"
#include "PiCanDB.hpp"
#include "CPUInfo.hpp"
#include "TempSensor.hpp"

using namespace std;


class PiCanMgr {
 
	static const char* 	PiCanMgr_Version;

	public:
	
	static PiCanMgr *shared() {
		if(!sharedInstance){
			sharedInstance = new PiCanMgr;
		}
		return sharedInstance;
	}
 
	PiCanMgr();
	~PiCanMgr();

	bool begin();
	void stop();

	DisplayMgr* display() {return _display;};
	AudioOutput* audio() {return _audio;};
	RadioMgr* radio() 	{return _radio;};
	PiCanDB * db() 	{return &_db;};
	
	void startTempSensor( std::function<void(bool didSucceed, std::string error_text)> callback = NULL);
	void stopTempSensor();
	PiCanMgrDevice::device_state_t tempSensor1State();

private:
	
	static PiCanMgr *sharedInstance;
	bool					_isSetup;

	bool 					_shouldQuit;				//Flag for starting and terminating the main loop
	pthread_t			_piCanLoopTID;
 
	void PiCanLoop();		// C++ version of thread
	// C wrappers for SDRReader;
	static void* PiCanLoopThread(void *context);
	static void PiCanLoopThreadCleanup(void *context);

	
	DisplayMgr* 		_display;
	AudioOutput*		_audio;
	RadioMgr*			_radio;
	
	PiCanDB 				_db;
	CPUInfo				_cpuInfo;
	TempSensor			_tempSensor1;

};

