//
//  PiCarMgr.c
//  carradio
//
//  Created by Vincent Moscaritolo on 5/8/22.
//

#include "PiCarMgr.hpp"
#include "PropValKeys.hpp"

#include <iostream>
#include <iostream>
#include <filesystem> // C++17
#include <fstream>

#include "Utils.hpp"

using namespace nlohmann;
using namespace Utils;

const char* 	PiCarMgr::PiCarMgr_Version = "1.0.0 dev 2";


const char* dev_display  = "/dev/ttyUSB0";
//const char* dev_audio  = "hw:CARD=wm8960soundcard,DEV=0";
const char* dev_audio  = "default";

constexpr int  pcmrate = 48000;



typedef void * (*THREADFUNCPTR)(void *);

PiCarMgr *PiCarMgr::sharedInstance = NULL;
 
static void sigHandler (int signum) {
	
	auto picanMgr = PiCarMgr::shared();
	picanMgr->stop();
}


PiCarMgr * PiCarMgr::shared() {
	if(!sharedInstance){
		sharedInstance = new PiCarMgr;
	}
	return sharedInstance;
}


PiCarMgr::PiCarMgr(){
	
 
//
	signal(SIGKILL, sigHandler);
//	signal(SIGHUP, sigHandler);
//	signal(SIGQUIT, sigHandler);
//	signal(SIGTERM, sigHandler);
//	signal(SIGINT, sigHandler);
 
	_stations.clear();
}

PiCarMgr::~PiCarMgr(){
	stop();
}

 
bool PiCarMgr::begin(){
	_isSetup = false;
		
	try {
  		_lastRadioMode = RadioMgr::MODE_UNKNOWN;
		_lastFreqForMode.clear();
		
		_display = new DisplayMgr();

		// clear DB
		_db.clearValues();

		// read in any properties
		_db.restorePropertiesFromFile();

		startCPUInfo();
 
		// if we fail, no big deal..
		startTempSensors();
		startControls();
		
		// setup audio out
		if(!_audio.begin(dev_audio ,pcmrate, true ))
			throw Exception("failed to setup Audio ");
		
		// quiet audio first
		if(!_audio.setVolume(0)
			|| ! _audio.setBalance(0))
			throw Exception("failed to setup Audio levels ");
			
		// find first RTS device
		auto devices = RtlSdr::get_devices();
		if( devices.size() == 0)
			throw Exception("No RTL devices found ");

		if(!_radio.begin(devices[0].index, pcmrate))
			throw Exception("failed to setup Radio ");
	
		// setup display device
		if(!_display->begin(dev_display,B9600))
			throw Exception("failed to setup Display ");
		
		// set initial brightness?
		if(!_display->setBrightness(5))
			throw Exception("failed to set brightness ");

		pthread_create(&_piCanLoopTID, NULL,
											  (THREADFUNCPTR) &PiCarMgr::PiCanLoopThread, (void*)this);

		triggerEvent(PGMR_EVENT_START);

		_display->showStartup();  // show startup
		
		restoreStationsFromFile();
		restoreRadioSettings();
		_isSetup = true;
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
		triggerEvent(PGMR_EVENT_EXIT);
		pthread_join(_piCanLoopTID, NULL);

		stopControls();
		stopTempSensors();
		stopCPUInfo();
		_radio.stop();
		_audio.setVolume(0);
		_audio.setBalance(0);
		_audio.stop();
		_display->stop();
	}
	
	_isSetup = false;

}


// MARK: -  convert to/from JSON
 
 
nlohmann::json PiCarMgr::GetAudioJSON(){
	json j;
	
	j[PROP_LAST_AUDIO_SETTING_VOL] =  _audio.volume();
	j[PROP_LAST_AUDIO_SETTING_BAL] =  _audio.balance();

	return j;
}

bool PiCarMgr::SetAudio(nlohmann::json j){
	bool success = false;
	
	if( j.is_object()
		&&  j.contains(PROP_LAST_AUDIO_SETTING_VOL)
		&&  j.at(PROP_LAST_AUDIO_SETTING_VOL).is_number()
		&&  j.contains(PROP_LAST_AUDIO_SETTING_BAL)
		&&  j.at(PROP_LAST_AUDIO_SETTING_BAL).is_number() ){
		auto vol = j[PROP_LAST_AUDIO_SETTING_VOL];
		auto bal = j[PROP_LAST_AUDIO_SETTING_BAL];
		
		_audio.setVolume(vol);
		_audio.setBalance(bal);

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

	if(_radio.radioMode() != RadioMgr::MODE_UNKNOWN
		&&  _radio.frequency() != 0) {
		
		auto mode = _radio.radioMode();
		
		if( _lastFreqForMode.count(mode)
			&& _lastFreqForMode[mode] == _radio.frequency() ){
			didUpdate = false;
		}
		else
		{
	 	_lastFreqForMode[mode] = _radio.frequency();
		_lastRadioMode = mode;
		didUpdate = true;
 		}
	}

	return didUpdate;
}

void PiCarMgr::saveRadioSettings(){

	updateRadioPrefs();
	
	_db.setProperty(PROP_LAST_RADIO_MODES, GetRadioModesJSON());
	_db.setProperty(PROP_LAST_RADIO_MODE, RadioMgr::modeString(_lastRadioMode));
 	_db.setProperty(PROP_LAST_AUDIO_SETTING, GetAudioJSON());
 }

void PiCarMgr::restoreRadioSettings(){
	
	nlohmann::json j = {};
	 
	// SET Audio
	if(!( _db.getJSONProperty(PROP_LAST_AUDIO_SETTING,&j)
		  && SetAudio(j))){
 		_audio.setVolume(.6);
		_audio.setBalance(0.0);
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
 
	string str;
	if(_db.getProperty(PROP_LAST_RADIO_MODE,&str)) {
 		auto mode = RadioMgr::stringToMode(str);
		_lastRadioMode = mode;
	}
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
			string location = trimCNTRL((v.size() >2) ?v[3]: string());
			
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

bool PiCarMgr::nextPresetStation(RadioMgr::radio_mode_t band,
								  uint32_t frequency,
								  bool up,
									station_info_t &info){
	
	if(_stations.count(band)){
		auto v = _stations[band];
		if(v.size() > 1)
			for ( auto i = v.begin(); i != v.end(); ++i ) {
				if(i->frequency == frequency){
					if(up){
						if(next(i) != v.end()){
							info = *(i+1);
							return true;
						}
					}
					else {
						if(i != v.begin()){
							info = *(i-1);
							return true;
							
						}
					}
				}
			}
	}
	return false;
}

// MARK: -  PiCarMgr main loop  thread
 
void PiCarMgr::triggerEvent(uint16_t evt ){
	pthread_mutex_lock (&_mutex);
		_event |= evt;
	pthread_cond_signal(&_cond);
	pthread_mutex_unlock (&_mutex);
}


void PiCarMgr::PiCanLoop(){
	
	constexpr struct timespec sleepTime  =  {0, 10000000};  // idle sleep in 100 hz
	constexpr time_t pollTime	= 5;  // poll sleep in seconds

	bool		 shouldQuit = false;
	timeval	 lastPollTime = {0,0};
 
	try{
		
		while(!shouldQuit){
			bool volMovedCW 		= false;
			bool volWasClicked 	= false;
			bool volWasMoved 		= false;
 			bool tunerMovedCW 	= false;
			bool tunerWasClicked = false;
			bool tunerWasMoved 	= false;
	
			_volKnob.updateStatus();
			volWasClicked = _volKnob.wasClicked();
			volWasMoved = 	_volKnob.wasMoved(volMovedCW);
			
			_tunerKnob.updateStatus();
			tunerWasClicked = _tunerKnob.wasClicked();
			tunerWasMoved 	= _tunerKnob.wasMoved(tunerMovedCW);

			// --check if any events need processing else wait for a timeout
			struct timespec ts = {0, 0};
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += sleepTime.tv_sec;
			ts.tv_nsec += sleepTime.tv_nsec;

			pthread_mutex_lock (&_mutex);
			// dont' wait if something is pending
			
			bool shouldWait = (_event == 0)
									&& !volWasClicked && !volWasMoved
									&& !tunerWasClicked && !tunerWasMoved;
			
			if (shouldWait)
				pthread_cond_timedwait(&_cond, &_mutex, &ts);
	 
			// startEvent is used for ignoring the pthread_cond_timedwait
			if((_event & PGMR_EVENT_START ) != 0){
				_event &= ~PGMR_EVENT_START;
			}
			
			if((_event & PGMR_EVENT_EXIT ) != 0){
				_event &= ~PGMR_EVENT_EXIT;
				shouldQuit = true;
			}
	 
			pthread_mutex_unlock (&_mutex);
 
			if(shouldQuit) continue;

			// Volume button Clicked
			if(volWasClicked){
				bool isOn = _radio.isOn();
				
				if(isOn){
					// just turn it off
					_radio.setON(false);
					
					// turn it off forces save of stations.
					saveRadioSettings();
					_db.savePropertiesToFile();
				}
				else {
		 			RadioMgr::radio_mode_t mode ;
					uint32_t freq;
					
					getSavedFrequencyandMode(mode,freq);
				 	_radio.setFrequencyandMode(mode, freq);
		 			_radio.setON(true);
				}
			}
			
			if(volWasMoved && _radio.isOn() ){
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
				
				_display->showVolumeChange();
			}
			
			if(tunerWasMoved && _radio.isOn() ){
				// change  stations
				bool shouldConstrain = true;
				
				auto newfreq = _radio.nextFrequency(tunerMovedCW, shouldConstrain);
				auto mode  = _radio.radioMode();
				_radio.setFrequencyandMode(mode, newfreq);
			}
				
//#if 0
//			if(wasClicked){
//				if(_display->isMenuDisplayed()){
//					_display->menuSelectAction(DisplayMgr::MENU_CLICK);
//				}
//				else{
//
//
//					vector<DisplayMgr::menuItem_t> items = {
//						"AM",
//						"FM",
//						"VHF",
//						"GPRS",
//						"-",
//						"GPS",
//						"Time",
//						"Diagnostics",
//						"Settings",
//						"Exit",
//					};
//
//					_display->showMenuScreen(items, 0, 10,
//													[=](bool didSucceed, uint selectedItem ){
//
//						if(didSucceed){
//							printf("Menu Completed |%s|\n", items[selectedItem].c_str());
//						}
//
//					});
//				}
//			}
//
//#else
//			// handle the fast stuff
//			if(wasClicked){
//				bool isOn = _radio.isOn();
//
//				if(isOn){
//					// just turn it off
//					_radio.setON(false);
//
//					// turn it off forces save of stations.
//					saveRadioSettings();
//					_db.savePropertiesToFile();
// 				}
//				else {
//					// if it has no setting we need to create something
//
//					_radio.setON(isOn);
//				}
////				_radio.setON(!isOn);
////				saveRadioSettings();
////				_db.savePropertiesToFile();
//			}
//
//#endif
//
//			if(wasMoved){
//
//				if(_display->isMenuDisplayed()){
//					_display->menuSelectAction(movedUp?DisplayMgr::MENU_UP:DisplayMgr::MENU_DOWN);
//		 		}
//				else
//				{
//#if 1
//					// change  channel
//					bool shouldConstrain = false;
//					if(_radio.isOn()){
//						auto newfreq = _radio.nextFrequency(movedUp, shouldConstrain);
//						auto mode  = _radio.radioMode();
//						_radio.setFrequencyandMode(mode, newfreq);
//						saveRadioSettings();
//
////						_lastRadioMode = mode;
////						_lastFreq = newfreq;
////						_radio.setFrequencyandMode(mode, newfreq);
////						saveRadioSettings();
//
//					}
//
//#elif 1
//					// change  volume
//					auto volume = _audio.volume();
//
//					if(movedUp){
//						if(volume < 1) {						// twist up
//							volume +=.04;
//							if(volume > 1) volume = 1.0;	// pin volume
//							_audio.setVolume(volume);
//							_db.updateValue(VAL_AUDIO_VOLUME, volume);
//						}
//					}
//					else {
//						if(volume > 0) {							// twist down
//							volume -=.04;
//							if(volume < 0) volume = 0.0;		// twist down
//							_audio.setVolume(volume);
//							_db.updateValue(VAL_AUDIO_VOLUME, volume);
//						}
//					}
//
//					_display->showVolumeChange();
//#else
//					// change  balance
//					auto balance = _audio.balance();
//
//					if(movedUp){
//						if(balance < 1) {						// twist up
//							balance +=.04;
//							if(balance > 1) balance = 1.0;	// pin volume
//							_audio.setBalance(balance);
//							_db.updateValue(VAL_AUDIO_BALANCE, balance);
//						}
//					}
//					else {
//
//						if(balance > -1) {							// twist down
//							balance -=.04;
//							if(balance < -1) balance = -1.;		// twist down
//							_audio.setBalance(balance);
//							_db.updateValue(VAL_AUDIO_BALANCE, balance);
//						}
//					}
//					_display->showBalanceChange();
//#endif
//				}
//
//			}
//
//
			// handle slower stuff like polling
			timeval now, diff;
			gettimeofday(&now, NULL);
			timersub(&now, &lastPollTime, &diff);
			
			if(diff.tv_sec >=  pollTime) {
				gettimeofday(&lastPollTime, NULL);
				
				// run idle first to prevent delays on first run.
				_tempSensor1.idle();
				_cpuInfo.idle();
				
				if(_tempSensor1.isConnected()){
					// handle input
					_tempSensor1.rcvResponse([=]( map<string,string> results){
						_db.updateValues(results);
					});
				}
				
				if(_cpuInfo.isConnected()){
					// handle input
					_cpuInfo.rcvResponse([=]( map<string,string> results){
						_db.updateValues(results);
					});
				}
				
				// ocassionally save properties
				if(_db.propertiesChanged()){
					_db.savePropertiesToFile();
				}
			}
		};

	}
	catch ( const Exception& e)  {
		printf("\tError %d %s\n\n", e.getErrorNumber(), e.what());
		
		if(e.getErrorNumber()	 == ENXIO){
	
		}
	}
	
	
//	while(!_shouldQuit){
//
//		if(!_isSetup){
//			usleep(200000);
//			continue;
//		}
//
//		// not doing anything yet.
//		sleep(1);
//	}
	
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


// MARK: -   Knobs and Buttons

void PiCarMgr::startControls( std::function<void(bool didSucceed, std::string error_text)> cb){
	int  errnum = 0;
	bool didSucceed = false;

	
	uint8_t leftAddress = 0x40;
	uint8_t rightAddress = 0x41;
 
	
	didSucceed =  _volKnob.begin(leftAddress, errnum)
			&&  	 _tunerKnob.begin(rightAddress, errnum);
	
	 
	if(didSucceed) {
 		_volKnob.setColor(0, 255, 0);
		_tunerKnob.setColor(0, 0, 255);

	}
	else {
		ELOG_MESSAGE("Could not start control knobs");
	}
	
	if(cb)
		(cb)(didSucceed, didSucceed?"": string(strerror(errnum) ));
}


void PiCarMgr::stopControls(){
	_volKnob.stop();
	_tunerKnob.stop();
}


// MARK: -   I2C Temp Sensors

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
