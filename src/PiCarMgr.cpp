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


// Duppa I2CEncoderV2 knobs
constexpr uint8_t leftKnobAddress = 0x40;
constexpr uint8_t rightKnobAddress = 0x41;

//const char* dev_audio  = "hw:CARD=wm8960soundcard,DEV=0";
//const char* dev_audio  = "hw:CARD=DAC,DEV=0";

const char* dev_audio  = "default";
constexpr int  pcmrate = 48000;

typedef void * (*THREADFUNCPTR)(void *);

PiCarMgr *PiCarMgr::sharedInstance = NULL;
 
static void sigHandler (int signum) {
	
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
	 
		if(!_gps.begin(path_gps,B9600, error))
			throw Exception("failed to setup GPS ", error);

		// setup display device
		if(!_display.begin(path_display,B9600))
			throw Exception("failed to setup Display ");
		
		// set initial brightness?
		if(!_display.setBrightness(7))
			throw Exception("failed to set brightness ");
 
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
				_volKnob.updateStatus(volKnobStatus);
				_tunerKnob.updateStatus(tunerKnobStatus);
				
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
			
			volWasClicked = _volKnob.wasClicked();
			volWasMoved = 	_volKnob.wasMoved(volMovedCW);
			tunerWasClicked = _tunerKnob.wasClicked();
			tunerWasMoved 	= _tunerKnob.wasMoved(tunerMovedCW);

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
					_display.LEDeventVol();
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
				
			_display.LEDeventVol();
	//		_display.showVolumeChange();
			}
			
			if(tunerWasMoved) {
				if(_display.isMenuDisplayed()){
					_display.menuSelectAction(tunerMovedCW?DisplayMgr::MENU_UP:DisplayMgr::MENU_DOWN);
			//
				}
				else if(_radio.isOn() ){
						// change  stations
					bool shouldConstrain = _stations.count(_radio.radioMode()) > 0;
	 
						auto newfreq = _radio.nextFrequency(tunerMovedCW, shouldConstrain);
						auto mode  = _radio.radioMode();
						_radio.setFrequencyandMode(mode, newfreq);
					}
			}
		
		 
			if(tunerWasClicked){
				
				if(_display.isMenuDisplayed()){
					_display.menuSelectAction(DisplayMgr::MENU_CLICK);
				}
				else{
					
					vector<DisplayMgr::menuItem_t> items = {
						"AM",
						"FM",
						"VHF",
						"GMRS",
						"-",
						"GPS",
						"Time",
						"Diagnostics",
						"Settings",
						"Exit",
					};
					
					_display.showMenuScreen(items, 0, 10,
													 [=](bool didSucceed, uint selectedItem ){
						
						if(didSucceed){
							printf("Menu Completed |%s|\n", items[selectedItem].c_str());
							
							saveRadioSettings();
							_db.savePropertiesToFile();

							switch(selectedItem){
								case 0:
								case 1:   // FM
								case 2:   // VHF
								case 3:   // GMRS
								{
									
									RadioMgr::radio_mode_t  mode = RadioMgr::MODE_UNKNOWN;
									uint32_t freq;
									switch(selectedItem){
										case 0: mode = RadioMgr::BROADCAST_AM; break;
										case 1: mode = RadioMgr::BROADCAST_FM; break;
										case 2: mode = RadioMgr::VHF; break;
										case 3: mode = RadioMgr::GMRS; break;
										default: break;
									}
									
						 
									if( ! getSavedFrequencyForMode(mode, freq) ){
										uint32_t maxFreq;
										RadioMgr:: freqRangeOfMode(mode, freq,maxFreq );
									}
									
									_radio.setFrequencyandMode(mode, freq, true);
									_radio.setON(true);
								}
								break;
 
								case 5: { // GPS
									_display.showGPS();
								}
									break;
									
								case 6: { // GPS
									_display.showTime();
								}
									break;
									
								case 7: { // DIAG
									_display.showDiag();
								}
									break;

							};
	
						}
						
					});
				}
			}
		};

	}
	catch ( const Exception& e)  {
		printf("\tError %d %s\n\n", e.getErrorNumber(), e.what());
		
		if(e.getErrorNumber()	 == ENXIO){
	
		}
	}
	
}
	// occasionally called durring idle time

void PiCarMgr::idle(){
	
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


// MARK: -   Knobs and Buttons

void PiCarMgr::startControls( std::function<void(bool didSucceed, std::string error_text)> cb){
	int  errnum = 0;
	bool didSucceed = false;
	
	
	if(! (  _volKnob.begin(leftKnobAddress, errnum)
		  && _tunerKnob.begin(rightKnobAddress, errnum)) ) {
		
		ELOG_MESSAGE("Could not start control knobs");
		goto done;
	}
	_volKnob.setColor(0, 255, 0);
	_tunerKnob.setColor(0, 0, 255);
	
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
