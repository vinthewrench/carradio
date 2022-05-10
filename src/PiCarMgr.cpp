//
//  PiCarMgr.c
//  carradio
//
//  Created by Vincent Moscaritolo on 5/8/22.
//

#include "PiCarMgr.hpp"
#include "PropValKeys.hpp"

#include <iostream>

using namespace nlohmann;

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
 
}

PiCarMgr::~PiCarMgr(){
	stop();
}

 
bool PiCarMgr::begin(){
	_isSetup = false;
		
	try {
 
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

nlohmann::json PiCarMgr::GetRadioJSON(){
	json j;
	
	j[PROP_LAST_RADIO_SETTING_FREQ] =  _radio.frequency();
	j[PROP_LAST_RADIO_SETTING_MODE] =  _radio.modeString (_radio.radioMode());
 	j[PROP_LAST_RADIO_SETTING_VOL] =  _audio.volume();
	j[PROP_LAST_RADIO_SETTING_BAL] =  _audio.balance();

	return j;
}

bool PiCarMgr::SetRadio(nlohmann::json j){
	bool success = false;
	
	return success;
}

bool PiCarMgr::saveRadioSettings(){
  	_db.setProperty(PROP_LAST_RADIO_SETTING, GetRadioJSON());
 	return true;
 }

bool PiCarMgr::restoreRadioSettings(){
	bool success = false;
	
	nlohmann::json j = {};
	
	// DEBUG FINISH HERE;
	
	if( _db.getJSONProperty(PROP_LAST_RADIO_SETTING,&j)
		&& !j.empty()) {
		
		if( j.contains(PROP_LAST_RADIO_SETTING_FREQ)
			&&  j.at(PROP_LAST_RADIO_SETTING_FREQ).is_number()
			&&  j.contains(PROP_LAST_RADIO_SETTING_MODE)
			&&  j.at(PROP_LAST_RADIO_SETTING_MODE).is_string() ){
			
			auto freq = j[PROP_LAST_RADIO_SETTING_FREQ];
 			auto mode = RadioMgr::stringToMode( j[PROP_LAST_RADIO_SETTING_MODE]);
			
			_lastFreq = freq;
			_lastRadioMode = mode;

			_radio.setFrequencyandMode(mode, freq);
 		}
		else {
 			_lastFreq = 0;
			_lastRadioMode = RadioMgr::RADIO_OFF;
			_radio.setFrequencyandMode(_lastRadioMode, _lastFreq);

		}
		
		if( j.contains(PROP_LAST_RADIO_SETTING_VOL)
			&&  j.at(PROP_LAST_RADIO_SETTING_VOL).is_number()
 			&&  j.contains(PROP_LAST_RADIO_SETTING_BAL)
			&&  j.at(PROP_LAST_RADIO_SETTING_BAL).is_number() ){
			
			auto vol = j[PROP_LAST_RADIO_SETTING_VOL];
			auto bal = j[PROP_LAST_RADIO_SETTING_BAL];
			
			_audio.setVolume(vol);
			_audio.setBalance(bal);
		}
		else {
			_audio.setVolume(.6);
			_audio.setBalance(0.0);
		}
		
	}
	
	return success;
}


// MARK: -  PiCarMgr main loop  thread
 
void PiCarMgr::triggerEvent(uint16_t evt ){
	pthread_mutex_lock (&_mutex);
		_event |= evt;
	pthread_cond_signal(&_cond);
	pthread_mutex_unlock (&_mutex);
}

void PiCarMgr::PiCanLoop(){
	
	constexpr struct timespec sleepTime  =  {0, 50 * 1000000};  // idle sleep in 50 millisconds
	constexpr time_t pollTime	= 5;  // poll sleep in seconds

	bool		 shouldQuit = false;
	timeval	 lastPollTime = {0,0};
 
	try{
		
		while(!shouldQuit){
			
			// --check if any events need processing else wait for a timeout
			struct timespec ts = {0, 0};
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += sleepTime.tv_sec;
			ts.tv_nsec += sleepTime.tv_nsec;

			pthread_mutex_lock (&_mutex);
			if (_event == 0)
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
			
		

			// handle the fast stuff
			if(_volKnob.wasClicked()){
				if(_radio.radioMode() != RadioMgr::RADIO_OFF){
					_radio.setFrequencyandMode(RadioMgr::RADIO_OFF);
				}
				else {
 					_radio.setFrequencyandMode(_lastRadioMode,_lastFreq);
				}
			}
			
			bool movedUp = false;
			if(_volKnob.wasMoved(movedUp)){
				
#if 1
				// change  channel
				
				auto mode  = _radio.radioMode();
				if(mode != RadioMgr::RADIO_OFF){
					auto newfreq = _radio.nextFrequency(movedUp);
					_lastRadioMode = mode;
					_lastFreq = newfreq;
					_radio.setFrequencyandMode(mode, newfreq);
				}
				
#elif 1
				// change  volume
				auto volume = _audio.volume();
				
				if(movedUp){
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
#else
				// change  balance
				auto balance = _audio.balance();
				
				if(movedUp){
					if(balance < 1) {						// twist up
						balance +=.04;
						if(balance > 1) balance = 1.0;	// pin volume
						_audio.setBalance(balance);
						_db.updateValue(VAL_AUDIO_BALANCE, balance);
					}
				}
				else {
					
					if(balance > -1) {							// twist down
						balance -=.04;
						if(balance < -1) balance = -1.;		// twist down
						_audio.setBalance(balance);
						_db.updateValue(VAL_AUDIO_BALANCE, balance);
					}
				}
				_display->showBalanceChange();
#endif
				
			}
			
			
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

	
	uint8_t deviceAddress = 0x3F;
 
	didSucceed =  _volKnob.begin(deviceAddress, errnum);
	if(didSucceed){
	 
	}
	else {
		ELOG_ERROR(ErrorMgr::FAC_DEVICE, deviceAddress, errnum,  "Start Controls");
	}
	
	
	if(cb)
		(cb)(didSucceed, didSucceed?"": string(strerror(errnum) ));
}


void PiCarMgr::stopControls(){
	_volKnob.stop();
	
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
