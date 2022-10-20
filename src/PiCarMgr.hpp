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
#include "PiCarCAN.hpp"
#include "W1Mgr.hpp"
#include "PropValKeys.hpp"

#include "PiCarDB.hpp"
#include "CPUInfo.hpp"
#include "ArgononeFan.hpp"

#if USE_TMP_117
#include "TempSensor.hpp"
#endif

#if USE_COMPASS
#include "CompassSensor.hpp"
#endif
#include "json.hpp"

using namespace std;
 
class PiCarMgr {
 

	public:
	
	static const char* 	PiCarMgr_Version;

	static PiCarMgr *shared();
 
	PiCarMgr();
	~PiCarMgr();

	bool begin();
	void stop();

	DisplayMgr* display() 	{return &_display;};
	AudioOutput* audio() {	return &_audio;};
	RadioMgr* 	radio() 		{return &_radio;};
	PiCarDB * 	db() 			{return &_db;};
	GPSmgr * 	gps()			{return &_gps;};
	PiCarCAN * 	can() 		{return &_can;};
	ArgononeFan* fan() 		{return &_fan;};
	W1Mgr*		 w1()			{return &_w1;};

	void startCPUInfo( std::function<void(bool didSucceed, std::string error_text)> callback = NULL);
	void stopCPUInfo();

	void startFan( std::function<void(bool didSucceed, std::string error_text)> callback = NULL);
	void stopFan();

#if USE_TMP_117
	void startTempSensors( std::function<void(bool didSucceed, std::string error_text)> callback = NULL);
	void stopTempSensors();
	PiCarMgrDevice::device_state_t tempSensor1State();
#endif
	
#if USE_COMPASS
	void startCompass( std::function<void(bool didSucceed, std::string error_text)> callback = NULL);
	void stopCompass();
	PiCarMgrDevice::device_state_t compassState();
 #endif
	
	void startControls( std::function<void(bool didSucceed, std::string error_text)> callback = NULL);
	void stopControls();

	void setDimLevel(double dimLevel);
	double dimLevel();
	
	void saveRadioSettings();
	void restoreRadioSettings();
 
	bool isAirPlayRunning();
	
	// MARK: - stations File
	
	typedef struct {
		RadioMgr::radio_mode_t	band;
		uint32_t						frequency;
		string						title;
		string						location;
		} station_info_t;

	bool restoreStationsFromFile(string filePath = "stations.tsv");
	bool getStationInfo(RadioMgr::radio_mode_t band, uint32_t frequency, station_info_t&);
	bool nextKnownStation(RadioMgr::radio_mode_t band,
								uint32_t frequency,
								bool up,
								station_info_t &info);

	bool setPresetChannel(RadioMgr::radio_mode_t mode, uint32_t  freq);
	bool clearPresetChannel(RadioMgr::radio_mode_t mode, uint32_t  freq);
	bool isPresetChannel(RadioMgr::radio_mode_t mode, uint32_t  freq);

	bool nextPresetStation(RadioMgr::radio_mode_t band,
									uint32_t frequency,
									bool up,
									station_info_t &info);

	
	
	
	bool  hasWifi(stringvector *ifnames = NULL);
	
	bool 	shouldSyncClockToGPS(time_t &deviation);
	
	bool clockNeedsSync(time_t deviation,  struct timespec gpsTime );
 
	bool setRelay1(bool state);
	
	// MARK: - scanner Channels
	
	bool setScannerChannel(RadioMgr::radio_mode_t mode, uint32_t  freq);
	bool clearScannerChannel(RadioMgr::radio_mode_t mode, uint32_t  freq);
	bool isScannerChannel(RadioMgr::radio_mode_t mode, uint32_t  freq);

	bool nextScannerStation(RadioMgr::radio_mode_t band,
									uint32_t frequency,
									bool up,
									station_info_t &info);

	vector< RadioMgr::channel_t> getScannerChannels() { return _scanner_freqs;};
 
	// MARK: - waypoints

	typedef struct  {
	string 			uuid;
	 string 			name;
	 GPSLocation_t location;
 } waypoint_prop_t;

 	vector< waypoint_prop_t> getWaypoints() { return _waypoints;};
	
private:
	
	typedef enum :int {
		MENU_UNKNOWN = 0,
		MENU_RADIO,
		MENU_SELECT_AUDIO_SOURCE,
		MENU_CANBUS,
		MENU_GPS,
		MENU_WAYPOINTS,
		MENU_AUDIO,
 		MENU_TIME,
		MENU_SETTINGS,
		MENU_INFO,
		MENU_DTC,
		MENU_EXIT
	} menu_mode_t;
	
	vector < pair<PiCarMgr::menu_mode_t, string>> _main_menu_map;
	int main_menu_map_offset(PiCarMgr::menu_mode_t);
	
	menu_mode_t currentMode();
	
	static PiCarMgr *sharedInstance;
	bool					_isSetup	= false;
	bool					_isRunning = false;
	
	nlohmann::json GetRadioModesJSON();
	bool updateRadioPrefs();
	void getSavedFrequencyandMode( RadioMgr::radio_mode_t &mode, uint32_t &freq);
	bool getSavedFrequencyForMode( RadioMgr::radio_mode_t mode, uint32_t &freqOut);
	void getWaypointProps();
	void updateWaypointProps();
	
	nlohmann::json GetRadioPresetsJSON();
	nlohmann::json GetScannerChannelJSON();
 
	void displayMenu();
	void displayRadioMenu();
	void displayAudioMenu();
	void displaySettingsMenu();
	void displayShutdownMenu();
	vector<string> settingsMenuItems();
	void setDisplayMode(menu_mode_t menuMode);
	
	void displayGPS();
	void displayWaypoints(string intitialUUID = "");
	void displayWaypoint(string uuid);

	void displayScannerChannels(RadioMgr::channel_t selectedChannel
										 	= {RadioMgr::MODE_UNKNOWN, 0 });
	
	void scannerChannelMenu(RadioMgr::channel_t selectedChannel);
	
	bool deleteWaypoint(string uuid);
	bool updateWaypoint(string uuid);
	bool createWaypoint(string name, waypoint_prop_t &wp );
	void sortWaypoints();
	
	void tunerDoubleClicked();
	void scannerDoubleClicked();
	
	void doShutdown();
	
	nlohmann::json GetAudioJSON();
	bool SetAudio(nlohmann::json j);
  
	pthread_t			_piCarLoopTID;
	void PiCarLoop();	 
	static void* PiCarLoopThread(void *context);
	static void PiCarLoopThreadCleanup(void *context);
	void idle();  // occasionally called durrig idle time

	// used for mapping 1-wire values to DB
	typedef struct {
		string 	deviceID;
		string 	dbKey;
		string	title;
	} w1_map_entry;
	map< string, w1_map_entry> _w1Map;
 
	RadioMgr::radio_mode_t					  _lastRadioMode;
	map <RadioMgr::radio_mode_t,uint32_t> _lastFreqForMode;
	menu_mode_t										_lastMenuMode;		// used for unwinding
	
	map<RadioMgr::radio_mode_t, vector<station_info_t>> _stations;
	vector < RadioMgr::channel_t >  _preset_stations;
  	vector < RadioMgr::channel_t >  _scanner_freqs;
  
	vector<waypoint_prop_t> _waypoints;
 
	typedef enum  {
	 TUNE_ALL = 0,
	 TUNE_KNOWN ,
	 TUNE_PRESETS,
	}tuner_knob_mode_t;
 
	tuner_knob_mode_t			_tuner_mode;

	DisplayMgr			_display;
	AudioOutput 		_audio;
	RadioMgr				_radio;
	PiCarDB 				_db;
	GPSmgr				_gps;
	PiCarCAN				_can;
	W1Mgr					_w1;
	
	CPUInfo				_cpuInfo;
#if USE_TMP_117
	TempSensor			_tempSensor1;
#endif
	
#if USE_COMPASS
	CompassSensor		_compass;
#endif
	
	ArgononeFan			_fan;
	
	bool					_autoShutdownMode;		 // controlled by canbus
	uint16_t				_shutdownDelay;		  //   seconds after no CAN activity

	bool					_autoDimmerMode;		 // controlled by canbus
	double				_dimLevel;		  //   0.0 - 1.0 fraction of bright

	bool					_isDayTime;			// for backlights
		
	bool 					_clocksync_gps;  		//  should sync clock with GPS
	time_t 				_clocksync_gps_secs;  // how many seconds of error allowed before sync
	
	struct timespec 	_startTime;
	struct timespec 	_lastActivityTime;

#if USE_GPIO_INTERRUPT
	struct gpiod_chip* 		_gpio_chip = NULL;
	struct gpiod_line*  		_gpio_line_int = NULL;
	struct gpiod_line*		_gpio_relay1;
#endif
};

