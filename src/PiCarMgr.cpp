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
#include <execinfo.h>
#include <time.h>
#include <sys/time.h>

#include "Utils.hpp"
#include "TimeStamp.hpp"

using namespace std;
using namespace timestamp;
using namespace nlohmann;
using namespace Utils;

const char* 	PiCarMgr::PiCarMgr_Version = "1.0.0 dev 8";


const char* path_display  = "/dev/ttyUSB0";

#if defined(__APPLE__)
const char* path_gps  = "/dev/cu.usbmodem14101";
#else
const char* path_gps  = "/dev/ttyACM0";
#endif

#if USE_GPIO_INTERRUPT
const char* 		gpioPath 				= "/dev/gpiochip0";
constexpr uint 	gpio_int_line_number	= 27;
const char*			 GPIOD_CONSUMER 		=  "gpiod-PiCar";
#endif
 

//const char* dev_audio  = "hw:CARD=wm8960soundcard,DEV=0";
//const char* dev_audio  = "hw:CARD=DAC,DEV=0";

//const char* dev_audio  = "default";

 const char* dev_audio  = "Headphones";

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
		  {MENU_AM, 		"AM"},
		  {MENU_FM, 		"FM"},
		  {MENU_VHF, 		"VHF"},
		  {MENU_GMRS, 	"GMRS"},
		  {MENU_UNKNOWN, "-"},
		  {MENU_GPS,		"GPS"},
		  {MENU_CANBUS,	"Engine"},
		  {MENU_TIME,		"Time"},
		  {MENU_UNKNOWN, "-"},
		  {MENU_SETTINGS,"Settings"},
		  {MENU_INFO,"Info"},
	  };

	_isRunning = true;

	pthread_create(&_piCanLoopTID, NULL,
											 (THREADFUNCPTR) &PiCarMgr::PiCanLoopThread, (void*)this);

	_stations.clear();
	
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
		
		// clear DB
		_db.clearValues();

		// read in any properties
		_db.restorePropertiesFromFile();
 		
		startCPUInfo();
 
		// if we fail, no big deal..
		startTempSensors();
		startCompass();
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
		if(devices.size() > 0) {
	 		if(!_radio.begin(devices[0].index))
			 throw Exception("failed to setup Radio ");
 		}
	 
		if(!_gps.begin(path_gps,B9600, error))
			throw Exception("failed to setup GPS ", error);

		// setup display device
		if(!_display.begin(path_display,B9600))
			throw Exception("failed to setup Display ");
		
		// set initial brightness?
		if(!_display.setBrightness(7))
			throw Exception("failed to set brightness ");
  
		// SETUP CANBUS
		if(!_can.begin(error))
			throw Exception("failed to setup CANBUS ", error);

		_display.showStartup();  // show startup
	
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
			printf("say hello\n");

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
		_display.stop();

		stopControls();
		stopCompass();
		stopTempSensors();
		stopCPUInfo();
		_audio.setVolume(0);
		_audio.setBalance(0);
		
		_radio.stop();
		_audio.stop();
	}
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
		_lastFreqForMode[mode] = _radio.frequency();
		_lastRadioMode = mode;
		didUpdate = true;
		 
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
 
void PiCarMgr::PiCanLoop(){
	
	constexpr time_t pollTime	= 1;  // polling for slow devices sleep in seconds
	bool firstRun = false;
	
	try{
		
		while(_isRunning){
			
			// if not setup // check back shortly -  we are starting up
			if(!_isSetup){
				usleep(10000);
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
			bool volWasMoved 		= false;
			bool tunerMovedCW 	= false;
			bool tunerWasClicked = false;
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
				timeout.tv_sec =  pollTime;
				timeout.tv_nsec = 0;
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
			volWasMoved 		= volKnob->wasMoved(volMovedCW);
			tunerWasClicked 	= tunerKnob->wasClicked();
			tunerWasMoved 		= tunerKnob->wasMoved(tunerMovedCW);
			
// MARK:   Volume button Clicked
			if(volWasClicked){
				
				if(_radio.isConnected()){
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
						_display.LEDeventVol();
						_display.showRadioChange();
						
						// save the new radio mode as a PROP_LAST_MENU_SELECTED
						auto newMenuSelect =  radioModeToMenuMode(mode);
						
						if(newMenuSelect != MENU_UNKNOWN)
							_db.setProperty(PROP_LAST_MENU_SELECTED, to_string(main_menu_map_offset(newMenuSelect)));
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
				//		_display.showVolumeChange();
			}
			
			
// MARK:   Tuner button moved
			if(tunerWasMoved) {
				
				if(_display.isScreenDisplayedMultiPage()
					&& _display.selectorKnobAction(tunerMovedCW?DisplayMgr::KNOB_UP:DisplayMgr::KNOB_DOWN)){
					// was handled - do nothing
				}
				// change tuner
				else if(_radio.isOn() ){
					// change  stations
					bool shouldConstrain = _stations.count(_radio.radioMode()) > 0;
					
					auto newfreq = _radio.nextFrequency(tunerMovedCW, shouldConstrain);
					auto mode  = _radio.radioMode();
					_radio.setFrequencyandMode(mode, newfreq);
				}
			}
			
// MARK:   Tuner button clicked
			if(tunerWasClicked){
				if(_display.isScreenDisplayedMultiPage()
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
 
	_compass.idle();
	_tempSensor1.idle();
	_cpuInfo.idle();
	
	if(_compass.isConnected()){
		// handle input
		_compass.rcvResponse([=]( map<string,string> results){
			
//			for (const auto& [key, value] : results) {
//				printf("Compass %s = %s\n", key.c_str(), value.c_str());
//			}
 
			_db.updateValues(results);
		});
	}

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

PiCarMgr::menu_mode_t PiCarMgr::radioModeToMenuMode(RadioMgr::radio_mode_t radioMode){
	menu_mode_t mode = MENU_UNKNOWN;

	switch(radioMode){
		case RadioMgr::BROADCAST_AM:
			mode = MENU_AM;
			break;
		case RadioMgr::BROADCAST_FM:
			mode = MENU_FM;
			break;
		case RadioMgr::VHF:
			mode = MENU_VHF;
			break;
		case RadioMgr::GMRS:
			mode = MENU_GMRS;
			break;
		default: break;
	}

	return mode;
}


PiCarMgr::menu_mode_t PiCarMgr::currentMode(){
	menu_mode_t mode = MENU_UNKNOWN;
	
	
	switch( _display.active_mode()){
			
		case DisplayMgr::MODE_RADIO:
			if(_radio.isOn()){
				mode  = radioModeToMenuMode(_radio.radioMode());
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
		case DisplayMgr::MODE_SETTINGS1:
			mode = MENU_SETTINGS;
			break;
			
		case DisplayMgr::MODE_CANBUS:
		case DisplayMgr::MODE_CANBUS1:
			mode = MENU_CANBUS;
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
 
	// fall back the selection for usability
	
	uint16_t lastSelect;
	if(_db.getUint16Property(PROP_LAST_MENU_SELECTED, &lastSelect)){
		selectedItem = lastSelect;
	}
	else if(selectedItem == -1
			  && (mode == MENU_TIME || mode == MENU_UNKNOWN))
		mode = MENU_FM;
	
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
									[=](bool didSucceed, uint newSelectedItem ){
 
		if(didSucceed) {
			menu_mode_t selectedMode = _main_menu_map[newSelectedItem].first;
			RadioMgr::radio_mode_t radioMode = RadioMgr::MODE_UNKNOWN;
			
			if(selectedMode != MENU_UNKNOWN)
				_db.setProperty(PROP_LAST_MENU_SELECTED, to_string(newSelectedItem));
 
			switch (selectedMode) {
 
				case MENU_AM:
	//				radioMode = RadioMgr::BROADCAST_AM;
					break;
					
				case MENU_FM:
					radioMode = RadioMgr::BROADCAST_FM;
					break;
					
				case MENU_VHF:
					radioMode = RadioMgr::VHF;
					break;
					
				case MENU_GMRS:
					radioMode = RadioMgr::GMRS;
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
 
				case 	MENU_UNKNOWN:
				default:
					// do nothing
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
				saveRadioSettings();
				_db.savePropertiesToFile();

			}
		}
	});
}


void PiCarMgr::displaySettingsMenu(){
	
	constexpr time_t timeout_secs = 10;
	
	vector<string> menu_items = {
			"Dim Screen",
			"Exit",
	};
 
	_display.showMenuScreen(menu_items,
									0,
									"Settings",
									timeout_secs,
									[=](bool didSucceed, uint newSelectedItem ){
	
		if(didSucceed) {
			
			switch (newSelectedItem) {
//				case 1:
// //					_display.showSettings(1);
//					break;
					
				default:
					break;
			}
	 
		}
	});
									
}
 

// MARK: -   Knobs and Buttons

void PiCarMgr::startControls( std::function<void(bool didSucceed, std::string error_text)> cb){
	int  errnum = 0;
	bool didSucceed = false;
	
#if USE_GPIO_INTERRUPT
	// setup GPIO lines
	_gpio_chip = gpiod_chip_open(gpioPath);
	if(!_gpio_chip) {
		ELOG_MESSAGE("Error open GPIO chip(\"%s\") : %s \n",gpioPath,strerror(errno));
		goto done;
	}
	
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
	
	if(_gpio_chip)
		gpiod_chip_close(_gpio_chip);
 #endif

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

// MARK: -   I2C Compass

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
