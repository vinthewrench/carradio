//
//  PiCanMgr.c
//  carradio
//
//  Created by Vincent Moscaritolo on 5/8/22.
//

#include "PiCanMgr.hpp"



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

PiCanMgr::PiCanMgr(){
	
	_display = new DisplayMgr();
	_audio 	= new AudioOutput();
	_radio	= new RadioMgr();
 
//
	signal(SIGKILL, sigHandler);
//	signal(SIGHUP, sigHandler);
//	signal(SIGQUIT, sigHandler);
//	signal(SIGTERM, sigHandler);
//	signal(SIGINT, sigHandler);
  
	
	_shouldQuit = false;
	pthread_create(&_piCanLoopTID, NULL,
								  (THREADFUNCPTR) &PiCanMgr::PiCanLoopThread, (void*)this);

}

PiCanMgr::~PiCanMgr(){
	
	_shouldQuit = true;
	pthread_join(_piCanLoopTID, NULL);
}

 
bool PiCanMgr::begin(){
	_isSetup = false;
		
	try {
		// clear DB
		_db.clearValues();

		// read in any properties
		_db.restorePropertiesFromFile();

		_display->showStartup();
	
		startCPUInfo();
		
		// setup display device
		if(!_display->begin(dev_display,B9600))
			throw Exception("failed to setup Display ");
		
		// set initial brightness?
		if(!_display->setBrightness(5))
			throw Exception("failed to set brightness ");
		
		// if we fail, no big deal..
		startTempSensor();
	 
		// setup audio out
		if(!_audio->begin(dev_audio ,pcmrate, true ))
			throw Exception("failed to setup Audio ");
		
		// quiet audio first
		if(!_audio->setVolume(0)
			|| ! _audio->setBalance(0))
			throw Exception("failed to setup Audio levels ");
			
		// find first RTS device
		auto devices = RtlSdr::get_devices();
		if( devices.size() == 0)
			throw Exception("No RTL devices found ");

		if(!_radio->begin(devices[0].index, pcmrate))
			throw Exception("failed to setup Radio ");
	 
		_display->showStartup();  // show it again
	

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
		stopTempSensor();
		stopCPUInfo();
		_radio->stop();
		_audio->setVolume(0);
		_audio->setBalance(0);
		_audio->stop();
	
		_display->stop();
		
	}
	
	_isSetup = false;

}

// MARK: -  PiCanMgr main loop  thread

 
void PiCanMgr::PiCanLoop(){
	
	constexpr long SLEEP_SEC = 1;  // idle sleep in seconds
 
	try{
		while(!_shouldQuit){

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
	
			sleep(SLEEP_SEC);
	 
			_tempSensor1.idle();
			_cpuInfo.idle();
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



// MARK: -   I2C Temp Sensors

void PiCanMgr::startCPUInfo( std::function<void(bool didSucceed, std::string error_text)> cb){
	
	int  errnum = 0;
	bool didSucceed = false;

	didSucceed =  _cpuInfo.begin(errnum);
	if(didSucceed){
		
		uint16_t queryDelay = 0;
		if(_db.getUint16Property(string(CPUInfo::PROP_CPU_TEMP_QUERY_DELAY), &queryDelay)){
			_cpuInfo.setQueryDelay(queryDelay);
		}
		
 	}
	else {
		ELOG_ERROR(ErrorMgr::FAC_SENSOR, 0, errnum,  "Start CPUInfo");
	}
	
	
	if(cb)
		(cb)(didSucceed, didSucceed?"": string(strerror(errnum) ));
	
	_cpuInfo.begin();

}

void PiCanMgr::stopCPUInfo(){
	_cpuInfo.stop();

}


void PiCanMgr::startTempSensor( std::function<void(bool didSucceed, std::string error_text)> cb){
	
	int  errnum = 0;
	bool didSucceed = false;
	
	constexpr string_view TEMPSENSOR_KEY = "TEMP_";
	
	uint8_t deviceAddress = 0x4A;
	string resultKey =  string(TEMPSENSOR_KEY) + to_hex(deviceAddress,true);
	
	didSucceed =  _tempSensor1.begin(deviceAddress, resultKey, errnum);
	if(didSucceed){
		
		uint16_t queryDelay = 0;
		if(_db.getUint16Property(string(TempSensor::PROP_TEMPSENSOR_QUERY_DELAY), &queryDelay)){
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

void PiCanMgr::stopTempSensor(){
	_tempSensor1.stop();
}

PiCanMgrDevice::device_state_t PiCanMgr::tempSensor1State(){
	return _tempSensor1.getDeviceState();
}
