//
//  PiCarMgr.h
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
#include "PiCarMgrDevice.hpp"
#include "DisplayMgr.hpp"
#include "AudioOutput.hpp"
#include "RadioMgr.hpp"
#include "PiCanDB.hpp"
#include "CPUInfo.hpp"
#include "TempSensor.hpp"
#include "QTKnob.hpp"

using namespace std;
 
class PiCarMgr {
 
	static const char* 	PiCarMgr_Version;

	public:
	
	static PiCarMgr *shared();
 
	PiCarMgr();
	~PiCarMgr();

	bool begin();
	void stop();

	DisplayMgr* display() {return _display;};
	AudioOutput* audio() {return &_audio;};
	RadioMgr* radio() 	{return &_radio;};
	PiCanDB * db() 	{return &_db;};
	
	void startCPUInfo( std::function<void(bool didSucceed, std::string error_text)> callback = NULL);
	void stopCPUInfo();

	void startTempSensors( std::function<void(bool didSucceed, std::string error_text)> callback = NULL);
	void stopTempSensors();
	PiCarMgrDevice::device_state_t tempSensor1State();

 	void startControls( std::function<void(bool didSucceed, std::string error_text)> callback = NULL);
	void stopControls();

private:
	
	static PiCarMgr *sharedInstance;
	bool					_isSetup;

	
	//  event thread
	#define PGMR_EVENT_EXIT	0x0001
 	void triggerEvent(uint16_t);

	pthread_t			_piCanLoopTID;
	pthread_cond_t 	_cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t 	_mutex = PTHREAD_MUTEX_INITIALIZER;
	uint16_t				_event = 0;
	void PiCanLoop();		// C++ version of thread
	// C wrappers for SDRReader;
	static void* PiCanLoopThread(void *context);
	static void PiCanLoopThreadCleanup(void *context);
	
	DisplayMgr* 		_display;
	AudioOutput 		_audio;
	RadioMgr				_radio;
	PiCanDB 				_db;
	CPUInfo				_cpuInfo;
	TempSensor			_tempSensor1;
	QTKnob				_volKnob;
	
};

