//
//  PiCarMgr.c
//  carradio
//
//  Created by Vincent Moscaritolo on 5/8/22.
//

#include "PiCarMgr.hpp"

#include <iostream>
#include <iostream>
#include <filesystem> // C++17
#include <fstream>
#include <execinfo.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <regex>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>

#include <sys/types.h>
#include <sys/wait.h>

#if defined(__APPLE__)
#else
#include <linux/reboot.h>
#include <sys/reboot.h>

#endif

#include "Utils.hpp"
#include "TimeStamp.hpp"

using namespace std;
using namespace timestamp;
using namespace nlohmann;
using namespace Utils;

const char* 	PiCarMgr::PiCarMgr_Version = "1.0.0 dev 10";


const char* path_display  = "/dev/ttyUSB0";

const char* 		gpioPath 				= "/dev/gpiochip0";
constexpr uint 	gpio_relay1_line_number	= 26;
#if USE_GPIO_INTERRUPT
constexpr uint 	gpio_int_line_number	= 27;
const char*			 GPIOD_CONSUMER 		=  "gpiod-PiCar";
#endif
 
constexpr int  pcmrate = 48000;

typedef void * (*THREADFUNCPTR)(void *);

PiCarMgr *PiCarMgr::sharedInstance = NULL;




static void CRASH_Handler()
{
	void *trace_elems[20];
	int trace_elem_count(backtrace( trace_elems, 20 ));
	char **stack_syms(backtrace_symbols( trace_elems, trace_elem_count ));
	for ( int i = 0 ; i < trace_elem_count ; ++i )
	{
		std::cout << stack_syms[i] << "\r\n";
	}
	free( stack_syms );
	
	exit(1);
}

static void sigHandler (int signum) {
	
	printf("%s sigHandler %d\n", TimeStamp(false).logFileString().c_str(), signum);
	
	// ignore hangup
	if(signum == SIGHUP)
		return;
	
	auto picanMgr = PiCarMgr::shared();
	picanMgr->stop();
	exit(0);
}


PiCarMgr * PiCarMgr::shared() {
	if(!sharedInstance){
		sharedInstance = new PiCarMgr;
	}
	return sharedInstance;
}


PiCarMgr::PiCarMgr(){
	signal(SIGKILL, sigHandler);
	signal(SIGHUP, sigHandler);
	signal(SIGQUIT, sigHandler);
	
	signal(SIGTERM, sigHandler);
	signal(SIGINT, sigHandler);
	
	std::set_terminate( CRASH_Handler );
	
	_main_menu_map = {
		{MENU_SELECT_AUDIO_SOURCE,	"Audio Source"},
		{MENU_GPS,		"GPS"},
		{MENU_CANBUS,	"Engine"},
		{MENU_DTC,		"Diagnostics"},
		{MENU_TIME,		"Time"},
		{MENU_INFO,		"Info"},
		{MENU_SETTINGS, "Settings"},
		{MENU_EXIT, 	 "Exit"}
	};
	
	_isRunning = true;
	
	pthread_create(&_piCanLoopTID, NULL,
						(THREADFUNCPTR) &PiCarMgr::PiCanLoopThread, (void*)this);
		
	_stations.clear();
	_preset_stations.clear();
	
}

PiCarMgr::~PiCarMgr(){
	stop();
	_isRunning = false;
	pthread_join(_piCanLoopTID, NULL);
}


bool PiCarMgr::begin(){
	_isSetup = false;
	
	try {
		int error = 0;
	 
		_lastRadioMode = RadioMgr::MODE_UNKNOWN;
		_lastFreqForMode.clear();
		_tuner_mode = TUNE_ALL;
		_dimLevel =  1.0; // full
		_isDayTime = true;
		_autoDimmerMode = false;
		
		// clear DB
		_db.clearValues();
		
		// read in any properties
		_db.restorePropertiesFromFile();
		
		// start 1Wire devices
		_w1.begin();
		
		startCPUInfo();
		startFan();
		
#if USE_TMP_117
		startTempSensors();
#endif
		
#if USE_COMPASS
		startCompass();
#endif
		startControls();
		

		// setup display device
		if(!_display.begin(path_display,B9600))
			throw Exception("failed to setup Display ");
		
		// set initial brightness?
		_display.setKnobBackLight(false);
		_display.setBrightness(_dimLevel);

		// SETUP CANBUS
		_can.begin();
 
		// find first RTS device
		auto devices = RtlSdr::get_devices();
		if(devices.size() > 0) {
			if(!_radio.begin(devices[0].index))
				throw Exception("failed to setup Radio ");
		}

	 #if USE_SERIAL_GPS
			 
	 #if defined(__APPLE__)
			 const char* path_gps  = "/dev/cu.usbmodem14101";
	 #else
			 const char* path_gps  = "/dev/ttyAMA1";
	 #endif
			 
			 if(!_gps.begin(path_gps, B38400, error))
				 throw Exception("failed to setup GPS.  error: %d", error);
	 #else
			 constexpr uint8_t  GPSAddress = 0x42;
			 if(!_gps.begin(GPSAddress, error))
				 printf("failed to setup GPS %d ", error);
	 #endif

	
//		_display.showStartup();  // show startup

		// setup audio out
		if(!_audio.begin(pcmrate, true ))
			throw Exception("failed to setup Audio ");
		
		// quiet audio first
		if(!_audio.setVolume(0)
			|| ! _audio.setBalance(0))
			throw Exception("failed to setup Audio levels ");
 
		restoreStationsFromFile();
		restoreRadioSettings();
 
		_isSetup = true;
		
		bool firstRunToday = true;
		time_t now = time(NULL);
		time_t lastRun = 0;
		
		if(_db.getTimeProperty(PROP_LAST_WRITE_DATE, &lastRun)){
			if( (now-lastRun) < 60 ){
				firstRunToday = false;
			}
		}
		
		if(firstRunToday){
			LOGT_INFO("Hello Moto\n");
	 
			//	//		_audio.playSound("BTL.wav", [=](bool success){
			//
			//				printf("playSound() = %d\n", success);
			//			});
			
		}
 
	}
	catch ( const Exception& e)  {
		
		// display error on fail..
		
		printf("\tError %d %s\n\n", e.getErrorNumber(), e.what());
	}
	catch (std::invalid_argument& e)
	{
		// display error on fail..
		
		printf("EXCEPTION: %s ",e.what() );
	}
	
	
	return _isSetup;
}

void PiCarMgr::stop(){
	
	if(_isSetup  ){
		_isSetup = false;
		
		_gps.stop();
		_can.stop();
		_w1.stop();
		_display.stop();
		
		stopControls();
#if USE_COMPASS
		stopCompass();
#endif
#if USE_TMP_117
		stopTempSensors();
#endif
		
		stopCPUInfo();
		stopFan();
		
		_audio.setVolume(0);
		_audio.setBalance(0);
		_audio.setFader(0);
		
		_radio.stop();
		_audio.stop();
	}
}


void PiCarMgr::doShutdown(){
	
	stop();

#if defined(__APPLE__)
	//	system("/bin/sh shutdown -P now");
#else
	sync();
	sleep(1);
	reboot(RB_POWER_OFF);
#endif
	exit(EXIT_SUCCESS);

}


// MARK: -  convert to/from JSON


nlohmann::json PiCarMgr::GetAudioJSON(){
	json j;
	
	double  vol = _audio.volume();
	double  bal = _audio.balance();
	double  fade = _audio.fader();
	
	// limit the precisopn on these
	bal = std::floor((bal * 100) + .5) / 100;
	vol = std::floor((vol * 100) + .5) / 100;
	fade = std::floor((fade * 100) + .5) / 100;

	j[PROP_LAST_AUDIO_SETTING_VOL] =  vol;
	j[PROP_LAST_AUDIO_SETTING_BAL] =  bal;
	j[PROP_LAST_AUDIO_SETTING_FADER] =  fade;
	
	return j;
}

bool PiCarMgr::SetAudio(nlohmann::json j){
	bool success = false;
	
	if( j.is_object()
		&&  j.contains(PROP_LAST_AUDIO_SETTING_VOL)
		&&  j.at(PROP_LAST_AUDIO_SETTING_VOL).is_number()
		&&  j.contains(PROP_LAST_AUDIO_SETTING_BAL)
		&&  j.at(PROP_LAST_AUDIO_SETTING_BAL).is_number()
		&&  j.contains(PROP_LAST_AUDIO_SETTING_FADER)
		&&  j.at(PROP_LAST_AUDIO_SETTING_FADER).is_number()
		){
		double  vol = j[PROP_LAST_AUDIO_SETTING_VOL];
		double  bal = j[PROP_LAST_AUDIO_SETTING_BAL];
		double  fade = j[PROP_LAST_AUDIO_SETTING_FADER];

		// limit the precisopn on these
		bal = std::floor((bal * 100) + .5) / 100;
		vol = std::floor((vol * 100) + .5) / 100;
		fade = std::floor((fade * 100) + .5) / 100;
	
		_audio.setVolume(vol);
		_audio.setBalance(bal);
		_audio.setFader(fade);
	
		success= true;
	}
	
	return success;
}

nlohmann::json PiCarMgr::GetRadioModesJSON(){
	json j;
	
	for (auto& entry : _lastFreqForMode) {
		json j1;
		j1[PROP_LAST_RADIO_MODES_MODE] = RadioMgr::modeString(entry.first);
		j1[PROP_LAST_RADIO_MODES_FREQ] =  entry.second;
		j.push_back(j1);
	}
	
	return j;
}

bool PiCarMgr::updateRadioPrefs() {
	bool didUpdate = false;
	
	if(_radio.radioMode() == RadioMgr::AUX){
		_lastFreqForMode[RadioMgr::AUX] = 0;
		_lastRadioMode = RadioMgr::AUX;
		didUpdate = true;
	}
	else if(_radio.radioMode() != RadioMgr::MODE_UNKNOWN
		&&  _radio.frequency() != 0) {
		
		auto mode = _radio.radioMode();
		_lastFreqForMode[mode] = _radio.frequency();
		_lastRadioMode = mode;
		didUpdate = true;
	}
	
	return didUpdate;
}

void PiCarMgr::saveRadioSettings(){
	
	updateRadioPrefs();
	
	_db.setProperty(PROP_SYNC_CLOCK_TO_GPS, _clocksync_gps?_clocksync_gps_secs: 0);

	if(_autoDimmerMode)
		_db.removeProperty(PROP_DIMMER_LEVEL);
	else
	{
		// force to tenths
		char buffer[8];
		sprintf(buffer, "%0.1f", _dimLevel );
 		_db.setProperty(PROP_DIMMER_LEVEL,atof(buffer));
	}
	
	_db.setProperty(PROP_AUTO_DIMMER_MODE, _autoDimmerMode);

	_db.setProperty(PROP_TUNER_MODE, _tuner_mode);
	_db.setProperty(PROP_LAST_RADIO_MODES, GetRadioModesJSON());
	_db.setProperty(PROP_LAST_RADIO_MODE, RadioMgr::modeString(_lastRadioMode));
	_db.setProperty(PROP_LAST_AUDIO_SETTING, GetAudioJSON());
	_db.setProperty(PROP_PRESETS, GetRadioPresetsJSON());
		 
}

void PiCarMgr::restoreRadioSettings(){
	
	nlohmann::json j = {};
	
	// SET GPS CLOCK SYNC PREFS
	{
		_clocksync_gps = false;
		_clocksync_gps_secs = 0;
		
		uint16_t syncVal = 0;
		if(_db.getUint16Property(PROP_SYNC_CLOCK_TO_GPS,&syncVal) && syncVal > 0){
			_clocksync_gps = true;
			_clocksync_gps_secs = syncVal;
		}
	}
	
	// SET Dimmer
	if(_db.getBoolProperty(PROP_AUTO_DIMMER_MODE,&_autoDimmerMode) && _autoDimmerMode){
		setDimLevel(1.0);
	}
	else {
		float dimLevel = 1.0;
		if(_db.getFloatProperty(PROP_DIMMER_LEVEL, &dimLevel)){
			setDimLevel(dimLevel);
		}
	}
	
	// SET Audio
	if(!( _db.getJSONProperty(PROP_LAST_AUDIO_SETTING,&j)
		  && SetAudio(j))){
		_audio.setVolume(.6);
		_audio.setBalance(0.0);
		_audio.setFader(0.0);
	}
	
	// SET RADIO Info
	
	_lastFreqForMode.clear();
	
	if(_db.getJSONProperty(PROP_LAST_RADIO_MODES,&j)
		&&  j.is_array()){
		
		for(auto item : j ){
			if(item.is_object()
				&&  item.contains(PROP_LAST_RADIO_MODES_MODE)
				&&  item[PROP_LAST_RADIO_MODES_MODE].is_string()
				&&  item.contains(PROP_LAST_RADIO_MODES_FREQ)
				&&  item[(PROP_LAST_RADIO_MODES_FREQ)].is_number()){
				
				auto mode = RadioMgr::stringToMode( item[PROP_LAST_RADIO_MODES_MODE]);
				auto freq = item[PROP_LAST_RADIO_MODES_FREQ] ;
				_lastFreqForMode[mode] = freq;
			}
		}
	}
	
	// SET Preset stations
	
	_preset_stations.clear();
	if(_db.getJSONProperty(PROP_PRESETS,&j)
		&&  j.is_array()){
		
		vector < pair<RadioMgr::radio_mode_t,uint32_t>>  presets;
		presets.clear();
		
		for(auto item : j ){
			if(item.is_object()
				&&  item.contains(PROP_PRESET_MODE)
				&&  item[PROP_PRESET_MODE].is_string()
				&&  item.contains(PROP_PRESET_FREQ)
				&&  item[(PROP_PRESET_FREQ)].is_number()){
				
				auto mode = RadioMgr::stringToMode( item[PROP_PRESET_MODE]);
				auto freq = item[PROP_PRESET_FREQ] ;
				
				presets.push_back(make_pair(mode, freq));
			}
		}
		
		//sort them in order of frequency
		if( presets.size() >0 ){
			sort(presets.begin(), presets.end(),
				  [] (const pair<RadioMgr::radio_mode_t,uint32_t>& a,
						const pair<RadioMgr::radio_mode_t,uint32_t>& b) { return a.second < b.second; });
		}
		
		_preset_stations = presets;
	}
	
	_tuner_mode = TUNE_ALL;
	uint16_t val = 0;
	if(_db.getUint16Property(PROP_TUNER_MODE, &val)){
		_tuner_mode = static_cast<tuner_knob_mode_t>(val);
	}
	
	string str;
	if(_db.getProperty(PROP_LAST_RADIO_MODE,&str)) {
		auto mode = RadioMgr::stringToMode(str);
		_lastRadioMode = mode;
	}
	
	// read the 1-wire device map
	_w1Map.clear();
	if(_db.getJSONProperty(PROP_W1_MAP,&j)
		&&  j.is_array()){
		for(auto item : j ){
			if(item.is_object()
				&&  item.contains(PROP_ID)
				&&  item[PROP_ID].is_string()
				&&  item.contains(PROP_KEY)
				&&  item[PROP_KEY].is_string()){
				
				auto deviceid  =   item[PROP_ID];
				auto dbKey 		  =   item[PROP_KEY];
				string title = dbKey;
				
				if(item.contains(PROP_TITLE)
					&&  item[PROP_TITLE].is_string()){
					title = item[PROP_TITLE];
				}
				
				w1_map_entry entry = {
					.deviceID = deviceid,
					.dbKey = dbKey,
					.title = title
				};
				
				_w1Map[deviceid] = entry;
			}
		}
	}
}

nlohmann::json PiCarMgr::GetRadioPresetsJSON(){
	json j;
	
	for (auto& entry : _preset_stations) {
		json j1;
		j1[PROP_PRESET_MODE] = RadioMgr::modeString(entry.first);
		j1[PROP_PRESET_FREQ] =  entry.second;
		j.push_back(j1);
	}
	

	return j;
}

bool PiCarMgr::setPresetChannel(RadioMgr::radio_mode_t mode, uint32_t  freq){
	
	if(!isPresetChannel(mode,freq)){
		
		auto presets = _preset_stations;
		
		presets.push_back(make_pair(mode, freq));
		
		// re-sort them
		sort(presets.begin(), presets.end(),
			  [] (const pair<RadioMgr::radio_mode_t,uint32_t>& a,
					const pair<RadioMgr::radio_mode_t,uint32_t>& b) { return a.second < b.second; });
		
		_preset_stations = presets;
		return true;
	}
	
	return false;
}

bool PiCarMgr::clearPresetChannel(RadioMgr::radio_mode_t mode, uint32_t  freq){
	
	for(auto it = _preset_stations.begin(); it != _preset_stations.end(); it++){
		if(it->first == mode && it->second == freq){
			_preset_stations.erase(it);
			return true;
		}
	}
	
	return false;
}

bool PiCarMgr::isPresetChannel(RadioMgr::radio_mode_t mode, uint32_t  freq){
	
	for(auto e: _preset_stations){
		if(e.first == mode && e.second == freq){
			return true;
		}
	}
	return false;
}


void PiCarMgr::getSavedFrequencyandMode( RadioMgr::radio_mode_t &modeOut, uint32_t &freqOut){
	
	if(_lastRadioMode != RadioMgr::MODE_UNKNOWN
		&& _lastFreqForMode.count(_lastRadioMode)){
		modeOut = _lastRadioMode;
		freqOut =  _lastFreqForMode[_lastRadioMode];
	}
	else {
		// set it to something;
		modeOut =  _lastRadioMode = RadioMgr::BROADCAST_FM;
		freqOut = _lastFreqForMode[_lastRadioMode] = 101500000;
	}
}

bool PiCarMgr::getSavedFrequencyForMode( RadioMgr::radio_mode_t mode, uint32_t &freqOut){
	bool success = false;
	
	if( _lastFreqForMode.count(mode)){
		freqOut =  _lastFreqForMode[mode];
		success = true;
	}
	
	return success;
}



// MARK: - stations File


bool PiCarMgr::restoreStationsFromFile(string filePath){
	bool success = false;
	
	std::ifstream	ifs;
	
	// create a file path
	if(filePath.size() == 0)
		filePath = "stations.tsv";
	
	try{
		string line;
		
		_stations.clear();
		
		// open the file
		ifs.open(filePath, ios::in);
		if(!ifs.is_open()) return false;
		
		while ( std::getline(ifs, line) ) {
			
			// split the line looking for a token: and rest and ignore comments
			line = Utils::trimStart(line);
			if(line.size() == 0) continue;
			if(line[0] == '#')  continue;
			
			vector<string> v = split<string>(line, "\t");
			if(v.size() < 3)  continue;
			
			RadioMgr::radio_mode_t mode =  RadioMgr::stringToMode(v[0]);
			uint32_t freq = atoi(v[1].c_str());
			
			string title = trimCNTRL(v[2]);
			string location = trimCNTRL((v.size() >2) ?v[2]: string());
			
			if(freq != 0
				&& mode != RadioMgr::MODE_UNKNOWN){
				station_info_t info = {mode, freq, title, location};
				_stations[mode].push_back(info);
				
			}
		}
		for( auto &[mode, items]: _stations){
			sort(items.begin(), items.end(),
				  [] (const station_info_t& a,
						const station_info_t& b) { return a.frequency < b.frequency; });
		}
		
		
		success = _stations.size() > 0;
		ifs.close();
	}
	catch(std::ifstream::failure &err) {
		ELOG_MESSAGE("READ stations:FAIL: %s", err.what());
		success = false;
	}
	return success;
}

bool PiCarMgr::getStationInfo(RadioMgr::radio_mode_t band,
										uint32_t frequency,
										station_info_t &info){
	
	if(_stations.count(band)){
		for(const auto& e : _stations[band]){
			if(e.frequency == frequency){
				info = e;
				return true;
			}
		}
	}
	
	return false;
}

bool PiCarMgr::nextKnownStation(RadioMgr::radio_mode_t band,
										  uint32_t frequency,
										  bool tunerMovedCW,
										  station_info_t &info){
	
	if(_stations.count(band) == 0 ) {
		// if there are no known frequencies  all then to fallback to all.
		info.band = band;
		info.frequency =  _radio.nextFrequency(tunerMovedCW);
		info.title = "";
		info.location = "";
		return true;
	}
	
	auto v = _stations[band];
	// scan for next known freq
	
	if(tunerMovedCW){
		
		for ( auto i = v.begin(); i != v.end(); ++i ) {
			if(frequency >= i->frequency)
				continue;
			
			info =  *(i);
			return true;;
		}
	}
	else {
		for ( auto i = v.rbegin(); i != v.rend(); ++i ) {
			if(frequency <= i->frequency)
				continue;
			info =  *(i);
			return true;;
		}
	}
	
	return false;
}


bool PiCarMgr::nextPresetStation(RadioMgr::radio_mode_t band,
											uint32_t frequency,
											bool tunerMovedCW,
											station_info_t &info){
	
	if(_preset_stations.size() == 0 ) {
		// if there are no known frequencies  all then to fallback to all.
		info.band = band;
		info.frequency =  _radio.nextFrequency(tunerMovedCW);
		info.title = "";
		info.location = "";
		return true;
	}
	
	auto v = _preset_stations;
	
	if(tunerMovedCW){
		
		for ( auto i = v.begin(); i != v.end(); ++i ) {
			if(frequency >= i->second)
				continue;
			
			info.title = "";
			info.location = "";
			info.band = i->first;
			info.frequency =  i->second;
			return true;;
		}
	}
	else {
		for ( auto i = v.rbegin(); i != v.rend(); ++i ) {
			if(frequency <= i->second)
				continue;
			
			info.title = "";
			info.location = "";
			info.band = i->first;
			info.frequency =  i->second;
			return true;;
		}
	}
	
	return false;
}

// MARK: -  PiCarMgr main loop  thread

void PiCarMgr::PiCanLoop(){
	
	PRINT_CLASS_TID;
	
 	bool firstRun = false;
	
	try{
		
		while(_isRunning){
			
			// if not setup // check back shortly -  we are starting up
			if(!_isSetup){
				usleep(200000);
				continue;
			}
			
			// call things that need to be setup,
			// run idle first to prevent delays on first run.
			
			if(!firstRun){
				idle();
				firstRun = true;
			}
			
			// knob management -
			
			DuppaKnob* tunerKnob =	_display.rightKnob();
			DuppaKnob* volKnob = 	_display.leftKnob();
			
			uint8_t volKnobStatus = 0;
			uint8_t tunerKnobStatus  = 0;
			
			bool volMovedCW 		= false;
			bool volWasClicked 	= false;
			bool volWasDoubleClicked 	= false;
			bool volWasMoved 		= false;
			
			bool tunerMovedCW 	= false;
			bool tunerWasClicked = false;
			bool tunerWasDoubleClicked 	= false;
			bool tunerWasMoved 	= false;
			
			// loop until status changes
			for(;;) {
				
				if(!_isSetup) break;
				
				// get status from knobs
				volKnob->updateStatus(volKnobStatus);
				tunerKnob->updateStatus(tunerKnobStatus);
				
				// if any status bit are set process them
				if( (volKnobStatus | tunerKnobStatus) != 0) break;
				
				// or take a nap
				
#if USE_GPIO_INTERRUPT
				int err = 0;
				
				struct timespec timeout;
				// Timeout in polltime seconds
				timeout.tv_sec =  0;
				timeout.tv_nsec = 5e8;  // .5 sec
				gpiod_line_event evt;
				
				// gpiod_line_event_wait return 0 if wait timed out,
				// -1 if an error occurred,
				//  1 if an event occurred.
				err = gpiod_line_event_wait(_gpio_line_int, &timeout);
				if(err == -1){
					break;
					//				throw Exception("gpiod_line_event_wait failed ");
				}
				// gpiod_line_event_wait only blocks until there's an event or a timeout
				// occurs, it does not read the event.
				// call gpiod_line_event_read  to consume the event.
				else if (err == 1) {
					gpiod_line_event_read(_gpio_line_int, &evt);
				}
				else if (err == 0){
					// timeout occured ..
					//  call idle when nothing else is going on
					idle();
				}
#else
				usleep(10000);
				idle();
#endif
			}
			
			// maybe we quit while I was asleep.  bail now
			if(!_isRunning) continue;
			
			volWasClicked 		= volKnob->wasClicked();
			volWasDoubleClicked = volKnob->wasDoubleClicked();
			volWasMoved 		= volKnob->wasMoved(volMovedCW);
			
			tunerWasClicked 	= tunerKnob->wasClicked();
			tunerWasDoubleClicked  = tunerKnob->wasDoubleClicked();
			tunerWasMoved 		= tunerKnob->wasMoved(tunerMovedCW);
			
			// MARK:   Volume button Clicked
			if(volWasDoubleClicked){
				// toggle mute
				
				if(_radio.isConnected() && _radio.isOn()){
					_audio.setMute(!_audio.isMuted());
					
					if(_audio.isMuted())
						_display.LEDeventMute();
					else
						_display.LEDeventVol();
				}
			}
			
			if(volWasClicked){
				
				if(_radio.isConnected()){
					bool isOn = _radio.isOn();
					
					if(isOn){
						// just turn it off
						_radio.setON(false);
						setRelay1(false);
						
						// always unmute after
						_audio.setMute(false);
						
						// stop any Mute blinking
						_display.LEDeventStop();
						
						// turn it off forces save of stations.
						saveRadioSettings();
						_db.savePropertiesToFile();
					}
					else {
						RadioMgr::radio_mode_t mode ;
						uint32_t freq;
						
						_audio.setMute(false);
						getSavedFrequencyandMode(mode,freq);
						_radio.setFrequencyandMode(mode, freq);
						_radio.setON(true);
						setRelay1(true);

						_display.LEDeventVol();
						_display.showRadioChange();
						_db.setProperty(PROP_LAST_MENU_SELECTED, to_string(main_menu_map_offset(MENU_RADIO)));
					}
				}
				else {
					auto devices = RtlSdr::get_devices();
					if(devices.size() == 0){
						printf("no Radio found\n");
						_display.showDevStatus();  // show startup
						
					}
					//					if(devices.size() > 0) {
					//						if(!_radio.begin(devices[0].index))
					
				}
				
			}
			
			// MARK:   Volume button moved
			if(volWasMoved && _radio.isOn() ){
				
				//quit mute
				if(_audio.isMuted())
					_audio.setMute(false);
				
				// change  volume
				auto volume = _audio.volume();
				
				if(volMovedCW){
					if(volume < 1) {						// twist up
						volume +=.04;
						if(volume > 1) volume = 1.0;	// pin volume
						_audio.setVolume(volume);
						_db.updateValue(VAL_AUDIO_VOLUME, volume);
					}
				}
				else {
					if(volume > 0) {							// twist down
						volume -=.04;
						if(volume < 0) volume = 0.0;		// twist down
						_audio.setVolume(volume);
						_db.updateValue(VAL_AUDIO_VOLUME, volume);
					}
				}
				
				_display.LEDeventVol();
				
				// if the radio was not displayed, set it there now
				if((_display.active_mode() != DisplayMgr::MODE_RADIO)){
					_display.showRadioChange();
				}
			}
			
			// MARK:   Tuner button moved
			if(tunerWasMoved) {
				
				if(_display.usesSelectorKnob()
					&& _display.selectorKnobAction(tunerMovedCW?DisplayMgr::KNOB_UP:DisplayMgr::KNOB_DOWN)){
					// was handled - do nothing
				}
				// change tuner
				else if((_display.active_mode() == DisplayMgr::MODE_RADIO) &&  _radio.isOn() ){
					
					auto nextFreq = _radio.frequency();
					auto mode 	   = _radio.radioMode();
					
					switch(_tuner_mode){
						case TUNE_ALL:
							nextFreq = _radio.nextFrequency(tunerMovedCW);
							break;
							
						case TUNE_KNOWN:
						{
							PiCarMgr::station_info_t info;
							if(nextKnownStation(mode, nextFreq, tunerMovedCW, info)){
								nextFreq = info.frequency;
							}
						}
							break;
							
						case TUNE_PRESETS:
							PiCarMgr::station_info_t info;
							if(nextPresetStation(mode, nextFreq, tunerMovedCW, info)){
								nextFreq = info.frequency;
								mode = info.band;
							}
							break;
					}
					
					_radio.setFrequencyandMode(mode, nextFreq);
				}
			}
			
			
			// MARK:   Tuner button clicked
			if(tunerWasDoubleClicked){
				
				if(_display.usesSelectorKnob()
					&& _display.selectorKnobAction(DisplayMgr::KNOB_DOUBLE_CLICK)){
					// was handled - do nothing
				}
				else{
					tunerDoubleClicked();
				}
 				continue;
			}
			
			if(tunerWasClicked){
				if(_display.usesSelectorKnob()
					&& _display.selectorKnobAction(DisplayMgr::KNOB_CLICK)){
					// was handled - do nothing
				}
				else{
					displayMenu();
				}
			}
		}
	}
	catch ( const Exception& e)  {
		printf("\tError %d %s\n\n", e.getErrorNumber(), e.what());
		
		if(e.getErrorNumber()	 == ENXIO){
			
		}
	}
	
}
// occasionally called durring idle time

void PiCarMgr::idle(){
	
#if USE_TMP_117
	_tempSensor1.idle();
#endif
	
	_cpuInfo.idle();
	_fan.idle();
	
#if USE_COMPASS
	_compass.idle();
	if(_compass.isConnected()){
		// handle input
		_compass.rcvResponse([=]( map<string,string> results){
			
			//			for (const auto& [key, value] : results) {
			//				printf("Compass %s = %s\n", key.c_str(), value.c_str());
			//			}
			
			_db.updateValues(results);
		});
	}
#endif
	
	// grab temp data from 1 wire
	{
		map<string,float> temps = {};
		
		if(_w1.getTemperatures(temps)){
			for( auto &[devID, temp]: temps){
				// find it's db entry name
				if(_w1Map.count(devID)){
					w1_map_entry map = _w1Map[devID];
					_db.updateValue(map.dbKey,temp);
				}
			}
			
		}
	}
#if USE_TMP_117
	if(_tempSensor1.isConnected()){
		// handle input
		_tempSensor1.rcvResponse([=]( map<string,string> results){
			_db.updateValues(results);
		});
	}
#endif
	
	if(_cpuInfo.isConnected()){
		// handle input
		_cpuInfo.rcvResponse([=]( map<string,string> results){
			_db.updateValues(results);
		});
	}
	
	if(_fan.isConnected()){
		// handle input
		_fan.rcvResponse([=]( map<string,string> results){
			_db.updateValues(results);
		});
	}
	
	// check for change in dimmer
	
	FrameDB*	fDB 	= can()->frameDB();
	string JK_DIMMER_SW = "JK_DIMMER_SW";
	string DAYTIME = "DAYTIME";
	
	bool isDayTime = false;
	
	string rawValue;
	if( fDB->valueWithKey(JK_DIMMER_SW, &rawValue)
		&& fDB->boolForKey(DAYTIME, isDayTime) ) {
		
		double dimSW = fDB->normalizedDoubleForValue(JK_DIMMER_SW,rawValue) / 100. ;
		
		// did anything change
		if(_isDayTime	!= isDayTime || dimSW != _dimLevel) {
			
			if(isDayTime){
				_display.setKnobBackLight(false);
				if( _autoDimmerMode ){
					setDimLevel(1.0);
					_display.setBrightness(_dimLevel);
				}
			}
			else {
				_display.setKnobBackLight(true);
				
				if( _autoDimmerMode ){
					if(dimSW != _dimLevel){
						setDimLevel(dimSW);
						// update the brightness
						_display.setBrightness(_dimLevel);
					}
				}
			}
			
			_isDayTime = isDayTime;
			
		}
	}
	
 
	
	// ocassionally save properties
	saveRadioSettings();
	if(_db.propertiesChanged()){
		_db.savePropertiesToFile();
	}
 }

void* PiCarMgr::PiCanLoopThread(void *context){
	PiCarMgr* d = (PiCarMgr*)context;
	
	//   the pthread_cleanup_push needs to be balanced with pthread_cleanup_pop
	pthread_cleanup_push(   &PiCarMgr::PiCanLoopThreadCleanup ,context);
	
	d->PiCanLoop();
	
	pthread_exit(NULL);
	
	pthread_cleanup_pop(0);
	return((void *)1);
}


void PiCarMgr::PiCanLoopThreadCleanup(void *context){
	//PiCarMgr* d = (PiCarMgr*)context;
	
	//	printf("cleanup sdr\n");
}



// MARK: -   Menu Management

PiCarMgr::menu_mode_t PiCarMgr::currentMode(){
	menu_mode_t mode = MENU_UNKNOWN;
	
	switch( _display.active_mode()){
			
		case DisplayMgr::MODE_RADIO:
			if(_radio.isOn()){
				mode = MENU_RADIO;
			}
			break;
			
		case DisplayMgr::MODE_TIME:
			mode = MENU_TIME;
			break;
			
		case DisplayMgr::MODE_GPS:
			mode = MENU_GPS;
			break;
			
		case DisplayMgr::MODE_INFO:
			mode = MENU_INFO;
			break;
			
		case DisplayMgr::MODE_SETTINGS:
			mode = MENU_SETTINGS;
			break;
			
		case DisplayMgr::MODE_CANBUS:
			mode = MENU_CANBUS;
			break;
			
		case DisplayMgr::MODE_DTC:
			mode = MENU_DTC;
			break;
			
		default:
			break;
	}
	
	return mode;
}


int PiCarMgr::main_menu_map_offset(PiCarMgr::menu_mode_t mode ){
	int selectedItem = -1;
	
	for(int i = 0; i < _main_menu_map.size(); i++){
		auto e = _main_menu_map[i];
		if(selectedItem == -1)
			if(e.first == mode) selectedItem = i;
	}
	
	return selectedItem;
}


void PiCarMgr::displayMenu(){
	
	constexpr time_t timeout_secs = 10;
	
	vector<string> menu_items = {};
	int selectedItem = -1;
	menu_mode_t mode = currentMode();
	_lastMenuMode = mode;
	
	// if the radio is on.. keep that mode
	if( mode == MENU_RADIO){
		// keep that mode.
	}
	else {
		// fall back the selection for usability
		uint16_t lastSelect;
		if(_db.getUint16Property(PROP_LAST_MENU_SELECTED, &lastSelect)){
			selectedItem = lastSelect;
		}
		else if(selectedItem == -1
				  && (mode == MENU_TIME || mode == MENU_UNKNOWN))
			mode = MENU_RADIO;
	}
	
	menu_items.reserve(_main_menu_map.size());
	
	for(int i = 0; i < _main_menu_map.size(); i++){
		auto e = _main_menu_map[i];
		menu_items.push_back(e.second);
	}
	
	if(selectedItem == -1)
		selectedItem = main_menu_map_offset(mode);
	
	_display.showMenuScreen(menu_items,
									selectedItem,
									"Select Screen",
									timeout_secs,
									[=](bool didSucceed,
										 uint newSelectedItem,
										 DisplayMgr::knob_action_t action ){
		
		if(didSucceed && action == DisplayMgr::KNOB_CLICK) {
			menu_mode_t selectedMode = _main_menu_map[newSelectedItem].first;
			
			if(selectedMode != MENU_UNKNOWN)
				_db.setProperty(PROP_LAST_MENU_SELECTED, to_string(newSelectedItem));
			
			setDisplayMode(selectedMode);
		}
	});
}

void PiCarMgr::setDisplayMode(menu_mode_t menuMode){
	
	switch (menuMode) {
			
		case MENU_RADIO:
			_display.showRadioChange();
			break;
			
		case MENU_SELECT_AUDIO_SOURCE:
			displayRadioMenu();
			break;
			
		case MENU_TIME:
			_display.showTime();
			break;
			
		case MENU_GPS:
			_display.showGPS();
			break;
			
		case MENU_CANBUS:
			_display.showCANbus(1);
			break;
			
		case MENU_SETTINGS:
			displaySettingsMenu();
			break;
			
		case MENU_INFO:
			_display.showInfo();
			break;
			
		case MENU_DTC:
			_display.showDTC();
			break;
			
		case MENU_EXIT:
			if(_radio.isOn())
				_display.showRadioChange();
				else _display.showTime();
			break;
			
			
		case 	MENU_UNKNOWN:
		default:
			// do nothing
			break;
	}
}


void PiCarMgr::displayRadioMenu(){
	constexpr time_t timeout_secs = 20;
	int selectedItem = -1;
	
	vector<string> menu_items = {
		"AM",
		"FM",
		"VHF",
		"GMRS",
		"AUX",
		"-",
		"Exit"
	};
	
	switch(_lastRadioMode){
		case  RadioMgr::BROADCAST_AM:
			selectedItem = 0;
			break;
			
		case  RadioMgr::BROADCAST_FM:
			selectedItem = 1;
			break;
			
		case  RadioMgr::VHF:
			selectedItem = 2;
			break;
			
		case  RadioMgr::GMRS:
			selectedItem = 3;
			break;
			
		case RadioMgr::AUX:
			selectedItem = 4;
			break;

			
		default:
			selectedItem = 5;
			break;
	}
	
	_display.showMenuScreen(menu_items,
									selectedItem,
									"Audio Source",
									timeout_secs,
									[=](bool didSucceed,
										 uint newSelectedItem,
										 DisplayMgr::knob_action_t action ){
		
		if(didSucceed && action == DisplayMgr::KNOB_CLICK) {
			
			RadioMgr::radio_mode_t  radioMode  = RadioMgr::MODE_UNKNOWN;
			
			switch (newSelectedItem) {
					
				case 0: // AM not supported yet
					//				radioMode  = RadioMgr::BROADCAST_AM;
					break;
					
				case 1: // FM
					radioMode  = RadioMgr::BROADCAST_FM;
					break;
					
				case 2: // VHF
					radioMode  = RadioMgr::VHF;
					break;
					
				case 3: // GMRS
					radioMode  = RadioMgr::GMRS;
					break;
	
				case 4: // AUX
					radioMode  = RadioMgr::AUX;
					break;
	 
				default:		//ignore
					break;
			}
			
			
			// if it was a radio selection, turn it one and select
			
			if(radioMode != RadioMgr::MODE_UNKNOWN){
				uint32_t freq;
				
				if( ! getSavedFrequencyForMode(radioMode, freq) ){
					uint32_t maxFreq;
					RadioMgr:: freqRangeOfMode(radioMode, freq,maxFreq );
				}
				_radio.setFrequencyandMode(radioMode, freq, true);
				_radio.setON(true);
				setRelay1(true);

				saveRadioSettings();
				_db.savePropertiesToFile();
				return;
			}
		}
		if(_lastMenuMode != MENU_UNKNOWN){
			// restore old mode thast was set in main menu
			setDisplayMode(_lastMenuMode);
		}
		else	// fallback
		{
			_display.showTime();
		}
	});
};

vector<string> PiCarMgr::settingsMenuItems(){
	string dim_entry = _autoDimmerMode ? "Dim Screen (auto)": "Dim Screen";
	
	vector<string> menu_items = {
		"Audio Balance",
		"Audio Fader",
		dim_entry,
		"Exit",
		"-",
		"Shutdown"
	};

	return menu_items;
}

void PiCarMgr::displaySettingsMenu(){
	
	constexpr time_t timeout_secs = 10;
	
		
	_display.showMenuScreen(settingsMenuItems(),
									2,
									"Settings",
									timeout_secs,
									[=](bool didSucceed,
										 uint newSelectedItem,
										 DisplayMgr::knob_action_t action ){
		
		if(didSucceed) {
			
			if(action){
				switch (newSelectedItem) {
						
					case 0:
						_display.showBalanceChange();
						break;

					case 1:
						_display.showFaderChange();
						break;

					case 2:
						if(action == DisplayMgr::KNOB_CLICK){
							if(!_autoDimmerMode)
								_display.showDimmerChange();
 						}
						else if(action == DisplayMgr::KNOB_DOUBLE_CLICK){
							_autoDimmerMode = !_autoDimmerMode;
							_display.updateMenuItems(settingsMenuItems());
 						}
						break;

					case 5:
						doShutdown();
						break;
						
						//				case 1:
						// //					_display.showSettings(1);
						//					break;
						
					default:
						
						if(_lastMenuMode != MENU_UNKNOWN){
							// restore old mode thast was set in main menu
							setDisplayMode(_lastMenuMode);
						}
						else	// fallback
						{
							_display.showTime();
						}
						break;
				}
				
			}
			
		}
	});
	
}

void PiCarMgr::tunerDoubleClicked(){
	DisplayMgr::mode_state_t dMode = _display.active_mode();
	
	if(dMode == DisplayMgr::MODE_RADIO
		&& _radio.isOn()){
		// display set/preset menu
		
		RadioMgr::radio_mode_t  mode  = _radio.radioMode();
		uint32_t 					freq =  _radio.frequency();
		
		constexpr time_t timeout_secs = 10;
		
		vector<string> menu_items = {
			(_tuner_mode ==  TUNE_ALL ?	"[All channels]": "All channels"),
			(_tuner_mode ==  TUNE_KNOWN ?"[Known stations]": "Known stations"),
			_preset_stations.size() == 0?"No Presets"
			: ((_tuner_mode ==  TUNE_PRESETS ?"[Presets]": "Presets")),
			"-",
			isPresetChannel(mode, freq)?"Remove Preset":"Add Preset",
			"-",
			"Clear all presets"
		};
		
		_display.showMenuScreen(menu_items,
										_tuner_mode,		// select the mode we are in - so triple click has no effect
										"Channel Presets",
										timeout_secs,
										[=](bool didSucceed,
											 uint newSelectedItem,
										  		DisplayMgr::knob_action_t action ){
			
			if(didSucceed && action == DisplayMgr::KNOB_CLICK) {
				
				switch (newSelectedItem) {
						
					case 0: // Tune All
						_tuner_mode = TUNE_ALL;
						saveRadioSettings();
						_db.savePropertiesToFile();
						break;
						
					case 1: // Tune known
						_tuner_mode = TUNE_KNOWN;
						saveRadioSettings();
						break;
						
					case 2: // Tune presets
						if(_preset_stations.size() > 0){
							_tuner_mode = TUNE_PRESETS;
							saveRadioSettings();
							_db.savePropertiesToFile();
						}
						break;
						
					case 4: // set/clear
					{
						RadioMgr::radio_mode_t  mode  = _radio.radioMode();
						uint32_t 					freq =  _radio.frequency();
						if( isPresetChannel(mode, freq)){
							
							if(clearPresetChannel(mode, freq))
								saveRadioSettings();
						}
						else
						{
							if(setPresetChannel(mode, freq))
								saveRadioSettings();
						}
					}
						
						break;
						
					case 6: // clear all presets
						// this needs to also take you off of presets mode
					default:
						break;
				}
				
			}
			
			_display.showRadioChange();
			
		});
	}
}



// MARK: -   Knobs and Buttons

void PiCarMgr::startControls( std::function<void(bool didSucceed, std::string error_text)> cb){
	int  errnum = 0;
	bool didSucceed = false;
	
	// setup GPIO lines
	_gpio_chip = gpiod_chip_open(gpioPath);
	if(!_gpio_chip) {
		ELOG_MESSAGE("Error open GPIO chip(\"%s\") : %s \n",gpioPath,strerror(errno));
		goto done;
	}

	_gpio_relay1 = gpiod_chip_get_line(_gpio_chip, gpio_relay1_line_number);
	if(!_gpio_relay1) {
		ELOG_MESSAGE("Error open GPIO line(%d) : %s \n",gpio_relay1_line_number,strerror(errno));
		goto done;
	}

	if( gpiod_line_request_output(_gpio_relay1, "gpio_relay_1", 0) != 0){
		ELOG_MESSAGE("Error gpiod_line_request_output line(%d) : %s \n",gpio_relay1_line_number,strerror(errno));
		goto done;
	}

	
#if USE_GPIO_INTERRUPT
	// get refs to the lines
	_gpio_line_int =  gpiod_chip_get_line(_gpio_chip, gpio_int_line_number);
	if(!_gpio_line_int){
		ELOG_MESSAGE("Error gpiod_chip_get_line %d: %s \n",  gpio_int_line_number, strerror(errno));
		goto done;
	}
	
	// setup the l;ine for input and select pull up resistor
	if(  gpiod_line_request_falling_edge_events_flags(_gpio_line_int,
																	  GPIOD_CONSUMER,
																	  GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP) != 0){
		ELOG_MESSAGE("Error gpiod_line_request_falling_edge_events %d: %s \n",  gpio_int_line_number, strerror(errno));
	}
	
	
#endif
	
	didSucceed = true;
done:
	
	if(cb)
		(cb)(didSucceed, didSucceed?"": string(strerror(errnum) ));
}


void PiCarMgr::stopControls(){
	
#if USE_GPIO_INTERRUPT
	if(_gpio_line_int)
		gpiod_line_release (_gpio_line_int);
	_gpio_line_int = NULL;
#endif
	
	if(_gpio_relay1)
		gpiod_line_release(_gpio_relay1);
	_gpio_relay1 = NULL;
 
	if(_gpio_chip)
		gpiod_chip_close(_gpio_chip);
	_gpio_chip = NULL;
}

void PiCarMgr::setDimLevel(double dimLevel){
	
	if(dimLevel < 0) dimLevel = 0;
	_dimLevel = dimLevel;
}

double PiCarMgr::dimLevel(){
	return _dimLevel;
}

bool PiCarMgr::setRelay1(bool state){
	bool didSucceed = false;
	
	if(_gpio_relay1) {
		gpiod_line_set_value(_gpio_relay1, state);
	}
	
	return didSucceed;
}




// MARK: -   Devices

void PiCarMgr::startCPUInfo( std::function<void(bool didSucceed, std::string error_text)> cb){
	
	int  errnum = 0;
	bool didSucceed = false;
	
	didSucceed =  _cpuInfo.begin(errnum);
	if(didSucceed){
		
		uint16_t queryDelay = 0;
		if(_db.getUint16Property(PROP_CPU_TEMP_QUERY_DELAY, &queryDelay)){
			_cpuInfo.setQueryDelay(queryDelay);
		}
		
	}
	else {
		ELOG_ERROR(ErrorMgr::FAC_SENSOR, 0, errnum,  "Start CPUInfo");
	}
	
	
	if(cb)
		(cb)(didSucceed, didSucceed?"": string(strerror(errnum) ));
	
}

void PiCarMgr::stopCPUInfo(){
	_cpuInfo.stop();
	
}

void PiCarMgr::startFan( std::function<void(bool didSucceed, std::string error_text)> cb){
	
	int  errnum = 0;
	bool didSucceed = false;
	
	didSucceed =  _fan.begin(errnum);
	if(didSucceed){
		
		uint16_t queryDelay = 0;
		if(_db.getUint16Property(PROP_FAN_QUERY_DELAY, &queryDelay)){
			_fan.setQueryDelay(queryDelay);
		}
		
	}
	else {
		ELOG_ERROR(ErrorMgr::FAC_SENSOR, 0, errnum,  "Start ArgononeFan");
	}
	
	
	if(cb)
		(cb)(didSucceed, didSucceed?"": string(strerror(errnum) ));
	
}

void PiCarMgr::stopFan(){
	_fan.stop();
}


#if USE_TMP_117


void PiCarMgr::startTempSensors( std::function<void(bool didSucceed, std::string error_text)> cb){
	
	int  errnum = 0;
	bool didSucceed = false;
	
	
	uint8_t deviceAddress = 0x4A;
	
	didSucceed =  _tempSensor1.begin(deviceAddress, VAL_OUTSIDE_TEMP, errnum);
	if(didSucceed){
		
		uint16_t queryDelay = 0;
		if(_db.getUint16Property(PROP_TEMPSENSOR_QUERY_DELAY, &queryDelay)){
			_tempSensor1.setQueryDelay(queryDelay);
		}
		
		//		_db.addSchema(resultKey,
		//						  CoopMgrDB::DEGREES_C, 2,
		//						  "Coop Temperature",
		//						  CoopMgrDB::TR_TRACK);
		
		//		LOGT_DEBUG("Start TempSensor 1 - OK");
	}
	else {
		ELOG_ERROR(ErrorMgr::FAC_SENSOR, deviceAddress, errnum,  "Start TempSensor 1 ");
	}
	
	
	if(cb)
		(cb)(didSucceed, didSucceed?"": string(strerror(errnum) ));
	
}

void PiCarMgr::stopTempSensors(){
	_tempSensor1.stop();
}

PiCarMgrDevice::device_state_t PiCarMgr::tempSensor1State(){
	return _tempSensor1.getDeviceState();
}
#endif

// MARK: -   I2C Compass
#if USE_COMPASS

void PiCarMgr::startCompass ( std::function<void(bool didSucceed, std::string error_text)> cb){
	
	int  errnum = 0;
	bool didSucceed = false;
	
	
	uint8_t deviceAddress = 0;  // use default
	
	didSucceed =  _compass.begin(deviceAddress, errnum);
	if(didSucceed){
		
		uint16_t queryDelay = 0;
		if(_db.getUint16Property(PROP_COMPASS_QUERY_DELAY, &queryDelay)){
			_compass.setQueryDelay(queryDelay);
		}
	}
	else {
		ELOG_ERROR(ErrorMgr::FAC_SENSOR, deviceAddress, errnum,  "Start Compass");
	}
	
	
	if(cb)
		(cb)(didSucceed, didSucceed?"": string(strerror(errnum) ));
}

void PiCarMgr::stopCompass(){
	_compass.stop();
}



PiCarMgrDevice::device_state_t PiCarMgr::compassState(){
	return _compass.getDeviceState();
}

#endif

// MARK: -   Network and system interface


bool  PiCarMgr::hasWifi(stringvector *ifnames){
	bool  has_wifi = false;
	
	struct ifaddrs *ifaddr, *ifa;
	int family, n;
	
	if(getifaddrs(&ifaddr) == 0){
		
		stringvector names;
		for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
			if (ifa->ifa_addr == NULL)
				continue;
			
			if(strncmp( ifa->ifa_name, "wlan", 4) == 0){
				
				string name = string(ifa->ifa_name);
				
				family = ifa->ifa_addr->sa_family;
				
				if (family == AF_INET || family == AF_INET6) {
					
					
					if (std::find(names.begin(), names.end(),name) == names.end()){
						names.push_back(string(ifa->ifa_name));
					}
				}
			}
		}
		
		freeifaddrs(ifaddr);
		
		if( names.size() > 0){
			
			*ifnames = names;
			has_wifi = true;
		}
	}
 
	return has_wifi;
}

// MARK: - realtime clock sync


bool 	PiCarMgr::shouldSyncClockToGPS(uint16_t &deviation){
	if( _clocksync_gps ){
		deviation = _clocksync_gps_secs;
		return true;
	}
	deviation = 0;
	return false;
}
 
bool PiCarMgr::clockNeedsSync(uint16_t deviation,  struct timespec gpsTime ){
	
	bool success = false;
	 
	if(_clocksync_gps  &&  deviation >= _clocksync_gps_secs ){
		
		int r = clock_settime(CLOCK_REALTIME, &gpsTime);
		if(r == 0){
			LOGT_INFO("Clock synced to GPS\n");
			success = true;
		}
		else {
			ELOG_ERROR(ErrorMgr::FAC_GPS, 0, errno, "clock sync failed");
		}
	}
 
	return success;
}
 
