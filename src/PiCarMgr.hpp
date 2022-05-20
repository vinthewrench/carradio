//
//  PiCarMgr.h
//  carradio
//
//  Created by Vincent Moscaritolo on 5/8/22.
//


#pragma once


#define USE_GPIO_INTERRUPT 1


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
 
#if USE_GPIO_INTERRUPT

#if defined(__APPLE__)
// used for cross compile on osx
#include "macos_gpiod.h"
#else
#include <gpiod.h>
#endif
#endif


#include "CommonDefs.hpp"
#include "PiCarMgrDevice.hpp"
#include "DisplayMgr.hpp"
#include "AudioOutput.hpp"
#include "RadioMgr.hpp"
#include "GPSmgr.hpp"

#include "PiCarDB.hpp"
#include "CPUInfo.hpp"
#include "TempSensor.hpp"
#include "DuppaKnob.hpp"
#include "json.hpp"

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
	PiCarDB * db() 		{return &_db;};
	GPSmgr * gps() 		{return &_gps;};

	void startCPUInfo( std::function<void(bool didSucceed, std::string error_text)> callback = NULL);
	void stopCPUInfo();

	void startTempSensors( std::function<void(bool didSucceed, std::string error_text)> callback = NULL);
	void stopTempSensors();
	PiCarMgrDevice::device_state_t tempSensor1State();

 	void startControls( std::function<void(bool didSucceed, std::string error_text)> callback = NULL);
	void stopControls();

	void saveRadioSettings();
	void restoreRadioSettings();
 
	// MARK: - stations File
	
	typedef struct {
		RadioMgr::radio_mode_t	band;
		uint32_t						frequency;
		string						title;
		string						location;
		} station_info_t;

	bool restoreStationsFromFile(string filePath = "stations.tsv");
	bool getStationInfo(RadioMgr::radio_mode_t band, uint32_t frequency, station_info_t&);
	bool nextPresetStation(RadioMgr::radio_mode_t band,
								uint32_t frequency,
								bool up,
								station_info_t &info);

	
	
private:
	
	static PiCarMgr *sharedInstance;
	bool					_isSetup	= false;
	bool					_isRunning = false;
	
	nlohmann::json GetRadioModesJSON();
	bool updateRadioPrefs();
	void getSavedFrequencyandMode( RadioMgr::radio_mode_t &mode, uint32_t &freq);
	bool getSavedFrequencyForMode( RadioMgr::radio_mode_t mode, uint32_t &freqOut);
	
	nlohmann::json GetAudioJSON();
	bool SetAudio(nlohmann::json j);
  
	pthread_t			_piCanLoopTID;
	void PiCanLoop();		// C++ version of thread
	// C wrappers for SDRReader;
	static void* PiCanLoopThread(void *context);
	static void PiCanLoopThreadCleanup(void *context);
		
	RadioMgr::radio_mode_t		_lastRadioMode;
 	map <RadioMgr::radio_mode_t,uint32_t> _lastFreqForMode;
	 
	map<RadioMgr::radio_mode_t, vector<station_info_t>> _stations;
	
	DisplayMgr* 		_display;
	AudioOutput 		_audio;
	RadioMgr				_radio;
	PiCarDB 				_db;
	GPSmgr				_gps;
	CPUInfo				_cpuInfo;
	TempSensor			_tempSensor1;
	DuppaKnob			_volKnob;
	DuppaKnob			_tunerKnob;

#if USE_GPIO_INTERRUPT
	struct gpiod_chip* 		_gpio_chip = NULL;
	struct gpiod_line*  		_gpio_line_int = NULL;
#endif
};

