//
//  PiCanMgr.c
//  carradio
//
//  Created by Vincent Moscaritolo on 5/8/22.
//

#include "PiCanMgr.hpp"
#include "PropValKeys.hpp"


const char* 	PiCanMgr::PiCanMgr_Version = "1.0.0 dev 2";


const char* dev_display  = "/dev/ttyUSB0";
//const char* dev_audio  = "hw:CARD=wm8960soundcard,DEV=0";
const char* dev_audio  = "default";

constexpr int  pcmrate = 48000;



typedef void * (*THREADFUNCPTR)(void *);

PiCanMgr *PiCanMgr::sharedInstance = NULL;
 
static void sigHandler (int signum) {
	
	auto picanMgr = PiCanMgr::shared();
	picanMgr->stop();
}


PiCanMgr * PiCanMgr::shared() {
	if(!sharedInstance){
		sharedInstance = new PiCanMgr;
	}
	return sharedInstance;
}


PiCanMgr::PiCanMgr(){
	
 
//
	signal(SIGKILL, sigHandler);
//	signal(SIGHUP, sigHandler);
//	signal(SIGQUIT, sigHandler);
//	signal(SIGTERM, sigHandler);
//	signal(SIGINT, sigHandler);
 
}

PiCanMgr::~PiCanMgr(){
	stop();
}

 
bool PiCanMgr::begin(){
	_isSetup = false;
		
	try {
 
		_display = new DisplayMgr();

		// clear DB
		_db.clearValues();

		// read in any properties
		_db.restorePropertiesFromFile();

	//	_display->showStartup();
	
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

		_display->showStartup();  // show it again
	
		pthread_create(&_piCanLoopTID, NULL,
											  (THREADFUNCPTR) &PiCanMgr::PiCanLoopThread, (void*)this);

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

void PiCanMgr::stop(){
	
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

// MARK: -  PiCanMgr main loop  thread
 
void PiCanMgr::triggerEvent(uint16_t evt ){
	pthread_mutex_lock (&_mutex);
		_event |= evt;
	pthread_cond_signal(&_cond);
	pthread_mutex_unlock (&_mutex);
}

void PiCanMgr::PiCanLoop(){
	
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
	 
			if((_event & PGMR_EVENT_EXIT ) != 0){
				_event &= ~PGMR_EVENT_EXIT;
				shouldQuit = true;
				continue;
			}
			pthread_mutex_unlock (&_mutex);
 
			double savedFreq = 101.900e6;

			// handle the fast stuff
			if(_volKnob.wasClicked()){
				if(_radio.radioMode() != RadioMgr::RADIO_OFF){
					_radio.setFrequencyandMode(RadioMgr::RADIO_OFF);
				}
				else {
					_radio.setFrequencyandMode(RadioMgr::BROADCAST_FM,savedFreq);
				}
	 
			}
			
			// handle slower stuff like polling
			timeval now, diff;
			gettimeofday(&now, NULL);
			timersub(&now, &lastPollTime, &diff);
			
			if(diff.tv_sec >=  pollTime) {
				gettimeofday(&lastPollTime, NULL);
				
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
				
				_tempSensor1.idle();
				_cpuInfo.idle();
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


void* PiCanMgr::PiCanLoopThread(void *context){
	PiCanMgr* d = (PiCanMgr*)context;

	//   the pthread_cleanup_push needs to be balanced with pthread_cleanup_pop
	pthread_cleanup_push(   &PiCanMgr::PiCanLoopThreadCleanup ,context);
 
	d->PiCanLoop();
	
	pthread_exit(NULL);
	
	pthread_cleanup_pop(0);
	return((void *)1);
}

 
void PiCanMgr::PiCanLoopThreadCleanup(void *context){
	//PiCanMgr* d = (PiCanMgr*)context;
 
//	printf("cleanup sdr\n");
}


// MARK: -   Knobs and Buttons

void PiCanMgr::startControls( std::function<void(bool didSucceed, std::string error_text)> cb){
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


void PiCanMgr::stopControls(){
	_volKnob.stop();
	
}


// MARK: -   I2C Temp Sensors

void PiCanMgr::startCPUInfo( std::function<void(bool didSucceed, std::string error_text)> cb){
	
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

void PiCanMgr::stopCPUInfo(){
	_cpuInfo.stop();

}


void PiCanMgr::startTempSensors( std::function<void(bool didSucceed, std::string error_text)> cb){
	
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

void PiCanMgr::stopTempSensors(){
	_tempSensor1.stop();
}

PiCanMgrDevice::device_state_t PiCanMgr::tempSensor1State(){
	return _tempSensor1.getDeviceState();
}
