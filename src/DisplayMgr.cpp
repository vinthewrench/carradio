//
//  DisplayMgr.cpp
//  vfdtest
//
//  Created by Vincent Moscaritolo on 4/25/22.
//

#include "DisplayMgr.hpp"
#include <string>
#include <iomanip>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <iostream>
#include <stdlib.h>
#include <cmath>
#include <algorithm>
#include <limits.h>
#include <regex>
#include <fstream>

#include <sys/utsname.h>
#include <arpa/inet.h>

#include "Utils.hpp"
#include "XXHash32.h"
#include "timespec_util.h"
#include "dbuf.hpp"

#include "PiCarMgr.hpp"
#include "PropValKeys.hpp"

#define TRY(_statement_) if(!(_statement_)) { \
printf("FAIL AT line: %d\n", __LINE__ ); \
}

typedef void * (*THREADFUNCPTR)(void *);

// Duppa I2CEncoderV2 knobs
constexpr uint8_t leftKnobAddress = 0x40;
constexpr uint8_t rightKnobAddress = 0x41;

constexpr uint8_t rightRingAddress = 0x60;
constexpr uint8_t leftRingAddress = 0x61;

constexpr uint8_t antiBounceDefault = 1;
constexpr uint8_t antiBounceSlow = 32;

constexpr uint8_t doubleClickTime = 50;   // 50 * 10 ms


static  const string moreUp = "\x1b\x98\x04\xfb\x1d";
static  const string moreDown = "\x1b\x98\x04\xf9\x1d";
static  const string moreNext = "\x1b\x98\x04\xfa\x1d";
static  const string morePrev = "\x1b\x98\x04\x60\x1d";


//  MACOS doesnt support pthread_condattr_setclock

#if defined(__APPLE__)
#define TIMEDWAIT_CLOCK CLOCK_REALTIME
#else

/*
 looks like there is a bug in Raspberry PI that causes pthread_cond_timedwait to
 never timeout when using CLOCK_MONOTONIC_RAW  - so fuck them use CLOCK_REALTIME
 */
//#define TIMEDWAIT_CLOCK CLOCK_MONOTONIC_RAW

#define TIMEDWAIT_CLOCK CLOCK_REALTIME

#endif


static string distanceString(double d) {
	
	char buffer[16] = {0};
	
	if(d < .02){ // feet
		sprintf( buffer ,"%d ft", (int) round(d * 5280));
	}else if(d < .06){ // yards
		sprintf( buffer ,"%d yds", (int) round(d * 1760));
	} else  if(d < 20) {
		sprintf( buffer ,"%.2f mi", d);
	} else {
		sprintf( buffer ,"%d mi", (int)round(d));
	}
	
	return string(buffer);
	
};


DisplayMgr::DisplayMgr(){
	_eventQueue = {};
	_ledEvent = 0;
	_isSetup = false;
	_isRunning = true;
	_dimLevel = 1.0;
	_lastAirplayStatusTime = {0,0};
	_airplayStatus = 0;
	_menuSliderCBInfo = NULL;
	_menuSelectionSliderCBInfo = NULL;

	pthread_create(&_updateTID, NULL,
						(THREADFUNCPTR) &DisplayMgr::DisplayUpdateThread, (void*)this);
	
	pthread_create(&_ledUpdateTID, NULL,
						(THREADFUNCPTR) &DisplayMgr::LEDUpdateThread, (void*)this);
	
	pthread_create(&_metaReaderTID, NULL,
						(THREADFUNCPTR) &DisplayMgr::MetaDataReaderThread, (void*)this);
	
	
}

DisplayMgr::~DisplayMgr(){
	stop();
	_isRunning = false;
	pthread_cond_signal(&_cond);
	pthread_join(_updateTID, NULL);
	
	pthread_cond_signal(&_led_cond);
	pthread_join(_ledUpdateTID, NULL);
	
	pthread_join(_metaReaderTID, NULL);
	
}


bool DisplayMgr::begin(const char* path, speed_t speed){
	int error = 0;
	
	return begin(path, speed, error);
}

bool DisplayMgr::begin(const char* path, speed_t speed,  int &error){
	
	_isSetup = false;
	
	if(!_vfd.begin(path,speed,error))
		throw Exception("failed to setup VFD ");
	
	if( !(_rightRing.begin(rightRingAddress, error)
			&& _leftRing.begin(leftRingAddress, error)
			&& _rightKnob.begin(rightKnobAddress, error)
			&& _leftKnob.begin(leftKnobAddress, error)
			)){
		throw Exception("failed to setup LEDrings ");
	}
	
	// flip the ring numbers
	_rightRing.setOffset(14,true);
	_leftRing.setOffset(14, true);		// slight offset for volume control of zero
	
	if( _vfd.reset()
		&& _rightRing.reset()
		&& _leftRing.reset()
		&& _rightRing.clearAll()
		&& _leftRing.clearAll())
		_isSetup = true;
	
	if(_isSetup) {
		
		_rightKnob.setAntiBounce(antiBounceDefault);
		_leftKnob.setAntiBounce(antiBounceDefault);
		
		_rightKnob.setDoubleClickTime(doubleClickTime);
		_leftKnob.setDoubleClickTime(doubleClickTime);
		
		_leftRing.reset();
		_rightRing.reset();
		
		// Set for normal operation
		_rightRing.setConfig(0x01);
		_leftRing.setConfig(0x01);
		
		// full scaling -- control current with global curent
		_rightRing.SetScaling(0xFF);
		_leftRing.SetScaling(0xFF);
		
		// set full bright
		_rightRing.SetGlobalCurrent(DuppaLEDRing::maxGlobalCurrent());
		_leftRing.SetGlobalCurrent(DuppaLEDRing::maxGlobalCurrent());
		
		// clear all values
		_rightRing.clearAll();
		_leftRing.clearAll();
		
		_eventQueue = {};
		_ledEvent = 0;
		
		if(_menuSliderCBInfo) free(_menuSliderCBInfo);
		_menuSliderCBInfo = NULL;
	
		if(_menuSelectionSliderCBInfo) free(_menuSelectionSliderCBInfo);
		_menuSelectionSliderCBInfo = NULL;
 
		resetMenu();
	 	showStartup();
 	}
	
	return _isSetup;
}


void DisplayMgr::stop(){
	
	if(_isSetup){
		
		if(_menuCB) _menuCB(false, 0, KNOB_EXIT);
		
		if(_menuSliderCBInfo){
			if(_menuSliderCBInfo->doneCB)
				(_menuSliderCBInfo->doneCB)(false);
			free(_menuSliderCBInfo);
			_menuSliderCBInfo = NULL;
		}
		
		if(_menuSelectionSliderCBInfo) {
			if(_menuSelectionSliderCBInfo->doneCB)
				(_menuSelectionSliderCBInfo->doneCB)(false);
			free(_menuSelectionSliderCBInfo);
			_menuSelectionSliderCBInfo = NULL;
		}

		resetMenu();
		_eventQueue = {};
		_ledEvent = 0;
		
		// shut down the display loop
		_isSetup = false;
		pthread_cond_signal(&_cond);
		
		usleep(200000);
		drawShutdownScreen();
		
		_rightKnob.stop();
		_leftKnob.stop();
		_rightRing.stop();
		_leftRing.stop();
		
		_vfd.setPowerOn(false);
		_vfd.stop();
	}
	
}


// MARK: -  LED Events

void DisplayMgr::LEDeventStartup(){
	ledEventSet(LED_EVENT_STARTUP, LED_EVENT_ALL);
}

void DisplayMgr::LEDeventVol(){
	ledEventSet(LED_EVENT_VOL,0);
}

void DisplayMgr::LEDeventMute(){
	ledEventSet(LED_EVENT_MUTE,0);
}

void DisplayMgr::LEDeventStop(){
	ledEventSet(LED_EVENT_STOP,LED_EVENT_ALL);
}


void DisplayMgr::LEDeventScannerStep(){
	ledEventSet(LED_EVENT_SCAN_STEP,0);
}

void DisplayMgr::LEDeventScannerHold(){
	ledEventSet(LED_EVENT_SCAN_HOLD,0);
}

void DisplayMgr::LEDeventScannerStop(){
	ledEventSet(LED_EVENT_SCAN_STOP,0);
}

void DisplayMgr::LEDTunerUp (bool pinned){
	ledEventSet(pinned?LED_EVENT_TUNE_UP_PIN: LED_EVENT_TUNE_UP ,0);
}

void DisplayMgr::LEDTunerDown (bool pinned){
	ledEventSet(pinned?LED_EVENT_TUNE_DOWN_PIN: LED_EVENT_TUNE_DOWN,0);
}

void DisplayMgr::runLEDEventStartup(){
	
	static uint8_t 	ledStep = 0;
	
	if( _ledEvent & LED_EVENT_STARTUP ){
		ledEventSet(LED_EVENT_STARTUP_RUNNING,LED_EVENT_ALL );
		
		ledStep = 0;
		_leftRing.clearAll();
		_rightRing.clearAll();
	}
	else if( _ledEvent & LED_EVENT_STARTUP_RUNNING ){
		if(ledStep < 24 ){
			
			DuppaLEDRing::led_block_t data = {{0,0,0}};
			data[mod(++ledStep, 24)] = {255,255,255};
			_leftRing.setLEDs(data);
			_rightRing.setLEDs(data);
		}
		else {
			ledEventSet(0, LED_EVENT_STARTUP_RUNNING);
			_leftRing.clearAll();
			_rightRing.clearAll();
		}
	}
}


void DisplayMgr::runLEDEventMute(){
	
	static timespec		lastEvent = {0,0};
	static bool blinkOn = false;
	AudioOutput*		audio 	= PiCarMgr::shared()->audio();
	
	if( _ledEvent & LED_EVENT_MUTE ){
		lastEvent = {0,0};
		blinkOn = false;
		ledEventSet(LED_EVENT_MUTE_RUNNING, LED_EVENT_MUTE);
	}
	
	// do the first cycle right away
	if( _ledEvent & LED_EVENT_MUTE_RUNNING ){
		
		struct timespec now, diff;
		clock_gettime(CLOCK_MONOTONIC, &now);
		diff = timespec_sub(now, lastEvent);
		
		uint64_t diff_millis = timespec_to_ms(diff);
		
		if(diff_millis >= 500 ){ // 2Hz
			clock_gettime(CLOCK_MONOTONIC, &lastEvent);
			
			blinkOn = !blinkOn;
			
			if(blinkOn){
				
				float volume =  audio->mutedVolume();
				// muted LED scales between 1 and 24
				int ledvol = volume*23;
				for (int i = 0 ; i < 24; i++)
					_leftRing.setGREEN(i, i <= ledvol?0xff:0 );
				
			}
			else {
				_leftRing.clearAll();
			}
		}
	}
}


void DisplayMgr::runLEDEventVol(){
	
	static timespec		startedEvent = {0,0};
	AudioOutput*		audio 	= PiCarMgr::shared()->audio();
	
	bool setVolume = false;
	if( _ledEvent & LED_EVENT_VOL ){
		
		clock_gettime(CLOCK_MONOTONIC, &startedEvent);
		ledEventSet(LED_EVENT_VOL_RUNNING, LED_EVENT_VOL | LED_EVENT_MUTE_RUNNING );
		setVolume = true;
	}
	
	if( _ledEvent & LED_EVENT_VOL_RUNNING ){
		
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		
		int64_t diff = timespec_to_ms(timespec_sub(now,startedEvent));
		if(setVolume){
			float volume =  audio->volume();
			// volume LED scales between 1 and 24
			int ledvol = volume*23;
			
			for (int i = 0 ; i < 24; i++) {
				_leftRing.setGREEN(i, i <= ledvol?0xff:0 );
			}
			
		}
		else if(diff > 800){
			ledEventSet(0, LED_EVENT_VOL_RUNNING);
			
			// scan the LEDS off
			for (int i = 0; i < 24; i++) {
				_leftRing.setColor( i, 0, 0, 0);
			}
			
		}
		
	}
}


void DisplayMgr::runLEDEventScanner(){
	
	static uint8_t 	ledStep = 0;
	static bool			inScannerMode = false;
	
	// did we enter scanner mode?
	if((_ledEvent & (LED_EVENT_SCAN_STEP | LED_EVENT_SCAN_HOLD)) &&  !inScannerMode){
		// run scannerMode intro animation
		_rightRing.clearAll();
		
		// scan the LEDS off
		for (int i = 0; i < 24; i++) {
			
			_rightRing.setColor( i, 255, 0, 0);
			usleep(5000);
			_rightRing.setColor( i, 0, 0, 0);
		}
		usleep(10000);
		inScannerMode = true;
	}
	
	if( _ledEvent & LED_EVENT_SCAN_STEP ){
		
		//	printf("SCAN STEP: %d %08x\n",ledStep, _ledEvent);
		
		// arew we already in a a scan sequence?
		if( _ledEvent & LED_EVENT_SCAN_RUNNING ){
		}
		else {
			ledStep = 0;
			_rightRing.clearAll();
		}
		
		
		DuppaLEDRing::led_block_t data = {{0,0,0}};
		data[ledStep] = {255,0,0};
		_rightRing.setLEDs(data);
		ledStep = mod(ledStep+1, 24);
		ledEventSet(LED_EVENT_SCAN_RUNNING, LED_EVENT_SCAN_STEP);
	}
	
	else 	if( _ledEvent & LED_EVENT_SCAN_HOLD ){
		
		//		printf("SCAN HOLD: %d %08x\n",ledStep, _ledEvent);
		inScannerMode = true;
		
		DuppaLEDRing::led_block_t data = {{0,0,0}};
		data[ledStep] = {0,255,0};
		_rightRing.setLEDs(data);
		ledEventSet(LED_EVENT_SCAN_RUNNING, LED_EVENT_SCAN_HOLD);
	}
	else 	if( _ledEvent & LED_EVENT_SCAN_STOP ){
		
		//		printf("SCAN STOP:%08x\n", _ledEvent);
		inScannerMode = false;
		
		ledEventSet(0, LED_EVENT_SCAN_RUNNING | LED_EVENT_SCAN_STOP | LED_EVENT_SCAN_HOLD );
		_rightRing.clearAll();
	}
}


void DisplayMgr::runLEDEventTuner(){
	
	static timespec		startedEvent = {0,0};
	
	static uint8_t 		offset =  0;
	bool didChange = false;
	bool didPin = false;
	
	
	if( _ledEvent & LED_EVENT_TUNE_UP ){
		offset = mod(++offset, 24);
		didChange = true;
		//		printf("LED_EVENT_TUNE_UP:  %08x\n" ,_ledEvent);
		clock_gettime(CLOCK_MONOTONIC, &startedEvent);
		ledEventSet(LED_EVENT_TUNE_RUNNING, LED_EVENT_TUNE_UP  );
	}
	else if( _ledEvent & LED_EVENT_TUNE_DOWN ){
		offset = mod(--offset, 24);
		didChange = true;
		clock_gettime(CLOCK_MONOTONIC, &startedEvent);
		//		printf("LED_EVENT_TUNE_DOWN:  %08x\n" ,_ledEvent);
		ledEventSet(LED_EVENT_TUNE_RUNNING, LED_EVENT_TUNE_DOWN  );
	}
	else if( _ledEvent & LED_EVENT_TUNE_UP_PIN ){
		clock_gettime(CLOCK_MONOTONIC, &startedEvent);
		
		didPin = true;
		clock_gettime(CLOCK_MONOTONIC, &startedEvent);
		ledEventSet(LED_EVENT_TUNE_RUNNING, LED_EVENT_TUNE_UP_PIN  );
		
	}
	else if( _ledEvent & LED_EVENT_TUNE_DOWN_PIN ){
		
		didPin = true;
		clock_gettime(CLOCK_MONOTONIC, &startedEvent);
		ledEventSet(LED_EVENT_TUNE_RUNNING, LED_EVENT_TUNE_DOWN_PIN  );
	}
	
	if(didPin) {
		for (int i = 0 ; i < 24; i++) {
			if( i == offset){
				_rightRing.setColor(i, 16, 16, 16);
			}
			else {
				_rightRing.setColor(i, 0, 0, 0);
			}
		}
	}
	else if(didChange){
		for (int i = 0 ; i < 24; i++) {
			uint8_t off1 =  mod(offset-1, 24);
			uint8_t off2 =  mod(offset+1, 24);
			
			if( i == offset){
				_rightRing.setColor(i, 0, 0, 255);
			}
			else if(i == off1) {
				_rightRing.setColor(i, 16, 16, 16);
			}
			else if(i == off2) {
				_rightRing.setColor(i, 16, 16, 16);
			}
			else {
				_rightRing.setColor(i, 0, 0, 0);
			}
		}
	}
	
	if( _ledEvent & LED_EVENT_TUNE_RUNNING ){
		
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		
		int64_t diff = timespec_to_ms(timespec_sub(now,startedEvent));
		
		if(diff > 800){
			ledEventSet(0, LED_EVENT_TUNE_RUNNING);
			
			//			printf("LED_EVENT_TUNE_OFF  %08x\n" ,_ledEvent);
			
			// scan the LEDS off
			for (int i = 0; i < 24; i++) {
				_rightRing.setColor( i, 0, 0, 0);
			}
		}
		
	}
}


void DisplayMgr::ledEventSet(uint64_t set, uint64_t reset){
	
	pthread_mutex_lock (&_led_mutex);
	_ledEvent &= ~reset;
	_ledEvent |= set;
	
	// 	printf("ledEventSet %08x %08x = %08x\n",set,reset,_ledEvent);
	
	pthread_mutex_unlock (&_led_mutex);
	// only signal if you are setting a flag
	if((set & LED_EVENT_MASK) != 0)
		pthread_cond_signal(&_led_cond);
}
void DisplayMgr::LEDUpdateLoop(){
	
	//	printf("start LEDUpdateLoop\n");
	PRINT_CLASS_TID;
	
	pthread_condattr_t attr;
	pthread_condattr_init( &attr);
#if !defined(__APPLE__)
	//pthread_condattr_setclock is not supported on macOS
	pthread_condattr_setclock( &attr, TIMEDWAIT_CLOCK);
#endif
	pthread_cond_init( &_led_cond, &attr);
	
	while(_isRunning){
		
		// if not setup // check back later
		if(!_isSetup){
			sleep(1);
			continue;
		}
		
		pthread_mutex_lock (&_led_mutex);
		
		// wait for event.
		
		while((_ledEvent & LED_EVENT_MASK) == 0){
			
			// delay for half second
			struct timespec ts = {0, 0};
			struct timespec now = {0, 0};
			clock_gettime(TIMEDWAIT_CLOCK, &now);
			
			long delay = 1000;
			
			if( _ledEvent & LED_STATUS_MASK)
				delay = 100;
			
			ts = timespec_add(now, timespec_from_ms(delay));
			// wait for _led_cond or time delay == ETIMEDOUT
			
			int result = pthread_cond_timedwait(&_led_cond, &_led_mutex, &ts);
			if(result){
				if(result != ETIMEDOUT){
					printf( "LEDUpdateLoop: pthread_cond_timedwait : %s\n", strerror(result));
				}
				
#if 0
				// debugging how pthread_cond_timedwait works
				struct timespec ts1 = {0, 0};
				clock_gettime(TIMEDWAIT_CLOCK, &ts1);
				printf("LEDUpdateLoop:  pthread_cond_timedwait delay = %ld\n",
						 timespec_to_ms(timespec_sub(ts1, now)));
#endif
				break;
			}
		}
		
		uint64_t theLedEvent =  _ledEvent;
		pthread_mutex_unlock (&_led_mutex);
		
		// run the LED effects
		
		if( theLedEvent & (LED_EVENT_STOP)){
			ledEventSet(0, LED_EVENT_STOP);
			_leftRing.clearAll();
			_rightRing.clearAll();
		}
		
		if( theLedEvent & (LED_EVENT_STARTUP | LED_EVENT_STARTUP_RUNNING))
			runLEDEventStartup();
		
		if( theLedEvent & (LED_EVENT_VOL | LED_EVENT_VOL_RUNNING))
			runLEDEventVol();
		
		if( theLedEvent & (LED_EVENT_MUTE | LED_EVENT_MUTE_RUNNING))
			runLEDEventMute();
		
		if( theLedEvent & (LED_EVENT_SCAN_STEP | LED_EVENT_SCAN_HOLD | LED_EVENT_SCAN_STOP))
			runLEDEventScanner();
		
		if( theLedEvent & (LED_EVENT_TUNE_UP | LED_EVENT_TUNE_DOWN
								 | LED_EVENT_TUNE_UP_PIN | LED_EVENT_TUNE_DOWN_PIN
								 | LED_EVENT_TUNE_RUNNING))
			runLEDEventTuner();
	}
}


void* DisplayMgr::LEDUpdateThread(void *context){
	DisplayMgr* d = (DisplayMgr*)context;
	
	//   the pthread_cleanup_push needs to be balanced with pthread_cleanup_pop
	pthread_cleanup_push(   &DisplayMgr::LEDUpdateThreadCleanup ,context);
	
	d->LEDUpdateLoop();
	
	pthread_exit(NULL);
	
	pthread_cleanup_pop(0);
	return((void *)1);
}


void DisplayMgr::LEDUpdateThreadCleanup(void *context){
}


// MARK: -  display tools


static uint8_t calculateRingCurrent(uint8_t level) {
	
	level = min(static_cast<int>(level), 7);
	
	uint8_t table[] = {10, 30, 50, 80, 100, 128, 200, 255};
	
	uint8_t current = table[level];
	
	return current;
	
}


bool DisplayMgr::setBrightness(double level) {
	
	bool success = false;
	
	if(_isSetup){
		_dimLevel = level;
		
		
		// vfd 0 -7
		uint8_t vfdLevel =  level * 7.0 ;
		uint8_t ledCurrent = calculateRingCurrent(vfdLevel);
		//	printf("setBrightness %f %d\n", level, ledCurrent);
		
		if(vfdLevel == 0) vfdLevel  = 1;
		success = _vfd.setBrightness(vfdLevel);
		
		_rightRing.SetScaling(ledCurrent);
		_leftRing.SetScaling(ledCurrent);
		
		_rightKnob.setBrightness(level);
		_leftKnob.setBrightness(level);
		
	}
	
	return success;
}

bool DisplayMgr::setKnobBackLight(bool isOn){
	_backlightKnobs = isOn;
	
	//	printf("setKnobBackLight %d\n", isOn);
	
	switch (_current_mode) {
			
		default:
			
			if(_backlightKnobs){
				setKnobColor(KNOB_RIGHT, RGB::Lime);
				setKnobColor(KNOB_LEFT, RGB::Lime);
			}
			else {
				setKnobColor(KNOB_RIGHT, RGB::Black);
				setKnobColor(KNOB_LEFT, RGB::Black);
			}
			
			
			
	}
	return true;
}



bool DisplayMgr::setKnobColor(knob_id_t knob, RGB color){
	bool success = false;
	
	//	printf("setKnobColor %d  (%3d,%3d,%3d)\n", knob, color.r, color.b, color.g);
	
	
	if(_isSetup){
		
		// calculate color vs brightness
		RGB effectiveColor = color;
		
		switch (knob) {
			case KNOB_RIGHT:
				success = _rightKnob.setColor(effectiveColor);
				break;
				
			case KNOB_LEFT:
				success =  _leftKnob.setColor(effectiveColor);
				break;
				
		}
	}
	
	return success;
}

 
static int dBm_to_bars(double dBm){
	
	int bars = 0;
	
	if(dBm >= -4) bars = 4;
	else 	if(dBm > -8) bars = 3;
	else 	if(dBm > -16) bars = 2;
	else 	if(dBm > -28) bars = 1;

	return bars;
}

void DisplayMgr::drawReceptionBars(uint8_t x,  uint8_t y, double dBm, bool displayNumber ){
	
 	static const uint8_t noBars[] 	= {0x1A,0x80, 0x18,0x09, 0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
	static const uint8_t oneBars[] 	= {0x1A,0x80, 0x18,0x09, 0x04,0x00,0x0C,0x00,0x00,0x00,0x00,0x00,0x00 };
	static const uint8_t twoBars[] 	= {0x1A,0x80, 0x18,0x09, 0x04,0x00,0x0C,0x00,0x1C,0x00,0x00,0x00,0x00 };
	static const uint8_t threeBars[] = {0x1A,0x80, 0x18,0x09, 0x04,0x00,0x0C,0x00,0x1C,0x00,0x3C,0x00,0x00  };
	static const uint8_t fourBars[] 	= {0x1A,0x80, 0x18,0x09, 0x04,0x00,0x0C,0x00,0x1C,0x00,0x3C,0x00,0x7C  };
 
	const uint8_t* image  = noBars;
	
	switch (dBm_to_bars(dBm)){
		case 0: image = noBars; break;
		case 1: image = oneBars; break;
		case 2: image = twoBars; break;
		case 3: image = threeBars; break;
		case 4: image = fourBars; break;
		default:;
	};
	
	_vfd.setCursor( x , y-6)	;
 	_vfd.writePacket(image, sizeof(noBars));
		
	_vfd.setFont(VFD::FONT_MINI);
 	_vfd.setCursor(x+11, y);
	if(dBm < 0)
		_vfd.printPacket("%-3d ", int(dBm));
	else
		_vfd.printPacket("     ");

}


// MARK: -  change modes

DisplayMgr::mode_state_t DisplayMgr::active_mode(){
	mode_state_t mode = MODE_UNKNOWN;
	
	if(isStickyMode(_current_mode))
		mode = _current_mode;
	else
		mode = _saved_mode;
	
	return mode;
}

void DisplayMgr::showStartup(){
	setEvent(EVT_PUSH, MODE_STARTUP );
}

void DisplayMgr::showTime(){
	setEvent(EVT_PUSH, MODE_TIME);
}

void DisplayMgr::showInfo( time_t timeout){
	_lineOffset = 0;
	_menuTimeout = timeout;

	setEvent(EVT_PUSH, MODE_INFO);
}

void DisplayMgr::showDevStatus(){
	setEvent(EVT_PUSH, MODE_DEV_STATUS );
}

void DisplayMgr::showDimmerChange(){
	setEvent(EVT_PUSH, MODE_DIMMER );
}

void DisplayMgr::showSquelchChange(){
	setEvent(EVT_PUSH, MODE_SQUELCH);
}

void DisplayMgr::showRadioChange(){
	setEvent(EVT_PUSH, MODE_RADIO );
}

void DisplayMgr::showAirplayChange(){
	if(_current_mode == MODE_RADIO)
		setEvent(EVT_NONE, MODE_RADIO );
}


void DisplayMgr::showScannerChange(bool force){
	
	if(force){
		setEvent(EVT_PUSH, MODE_SCANNER );
	}
	else {
		setEvent(EVT_NONE, MODE_SCANNER );
	}
}

void DisplayMgr::showDTC(){
	setEvent(EVT_PUSH, MODE_DTC);
}

void DisplayMgr::showDTCInfo(string code){
	setEvent(EVT_PUSH, MODE_DTC_INFO, code);
}

void DisplayMgr::showGPS(knobCallBack_t cb){
	_knobCB = cb;
	setEvent(EVT_PUSH, MODE_GPS);
}

void DisplayMgr::showCANbus(uint8_t page){
	_currentPage = page;
	setEvent(EVT_PUSH, MODE_CANBUS);
}

void DisplayMgr::showMessage(string message,  time_t timeout, voidCallback_t cb){
	_simpleCB = cb;
	_menuTitle = message;
	_menuTimeout = timeout;
	
	setEvent(EVT_PUSH, MODE_MESSAGE);
}

void DisplayMgr::editString(string title, string strIn,
									 editStringCallBack_t cb){
	_editCB = cb;
	_editString = strIn;
	_menuTitle = title;
	
	setEvent(EVT_PUSH, MODE_EDIT_STRING);
}


void DisplayMgr::setEvent(event_t evt,
								  mode_state_t mod,
								  string arg){
	
	pthread_mutex_lock (&_mutex);
	
	// dont keep pushing the same thing
	bool shouldPush = true;
	if(!_eventQueue.empty()){
		auto item = _eventQueue.back();
		if(item.evt == evt &&  item.mode == mod ){
			shouldPush = false;
		}
	}
	
	if(shouldPush)
		_eventQueue.push({evt,mod, arg});
	
	pthread_mutex_unlock (&_mutex);
	
	if(shouldPush)
		pthread_cond_signal(&_cond);
}

// MARK: -  Knob Management

bool  DisplayMgr::usesSelectorKnob(){
	switch (_current_mode) {
		case MODE_CANBUS:
		case MODE_GPS:
		case MODE_GPS_WAYPOINT:
		case MODE_CHANNEL_INFO:
		case MODE_GPS_WAYPOINTS:
		case MODE_SCANNER_CHANNELS:
		case MODE_DIMMER:
		case MODE_SQUELCH:
		case MODE_EDIT_STRING:
		case MODE_DTC:
		case MODE_DTC_INFO:
		case MODE_MENU:
		case MODE_SLIDER:
		case MODE_SELECT_SLIDER:
		case MODE_INFO:
 			return true;
			
		default:
			return false;
	}
}


bool DisplayMgr::selectorKnobAction(knob_action_t action){
	
	bool wasHandled = false;
	// printf("selectorKnobAction (%d)\n", action);
	
	if(usesSelectorKnob()){
		if(_current_mode == MODE_MENU){
			wasHandled =  menuSelectAction(action);
		}
		else if(isMultiPage(_current_mode)){
			if(action == KNOB_UP){
				if(_currentPage < (pageCountForMode(_current_mode) -1 )) {
					_currentPage++;
					setEvent(EVT_REDRAW, _current_mode );
					wasHandled = true;
				}
			}
			else if(action == KNOB_DOWN){
				if(_currentPage > 0) {
					_currentPage--;
					setEvent(EVT_REDRAW, _current_mode );
					wasHandled = true;
				}
				
			}
			else if(action == KNOB_CLICK){
				wasHandled = processSelectorKnobAction(action);
			}
		}
		else {
			wasHandled = processSelectorKnobAction(action);
		}
	}
	
	return wasHandled;
}

uint8_t DisplayMgr::pageCountForMode(mode_state_t mode){
	uint8_t count = 1;
	
	PiCarMgr*			mgr 	= PiCarMgr::shared();
	
	switch (mode) {
		case MODE_CANBUS:
		{
			PiCarDB*		db 	= mgr->db();
			div_t d = div(db->canbusDisplayPropsCount(), 6);
			count +=  d.quot + (d.rem ? 1 : 0);
		}
			break;
			
		default :
			count = 1;
	}
	
	return count;
}



bool DisplayMgr::isMultiPage(mode_state_t mode){
	
	bool result = false;
	
	switch (mode) {
		case MODE_CANBUS:
			result = true;
			break;
			
		default :
			result = false;
	}
	
	return result;
}

bool DisplayMgr::processSelectorKnobAction( knob_action_t action){
	bool wasHandled = false;
	
	switch (_current_mode) {
		case MODE_SLIDER:
			wasHandled = processSelectorKnobActionForSlider(action);
			break;

		case MODE_SELECT_SLIDER:
			wasHandled = processSelectorKnobActionForSelectSlider(action);
			break;

		case MODE_SQUELCH:
			wasHandled = processSelectorKnobActionForSquelch(action);
			break;
			
		case MODE_DIMMER:
			wasHandled = processSelectorKnobActionForDimmer(action);
			break;
			
		case MODE_DTC:
			wasHandled = processSelectorKnobActionForDTC(action);
			break;
			
		case MODE_GPS_WAYPOINTS:
			wasHandled = processSelectorKnobActionForGPSWaypoints(action);
			break;
			
		case MODE_GPS_WAYPOINT:
			wasHandled = processSelectorKnobActionForGPSWaypoint(action);
			break;
			
		case MODE_GPS:
			wasHandled = processSelectorKnobActionForGPS(action);
			break;
			
		case MODE_DTC_INFO:
			wasHandled = processSelectorKnobActionForDTCInfo(action);
			break;
			
		case MODE_EDIT_STRING:
			wasHandled = processSelectorKnobActionForEditString(action);
			break;
			
		case MODE_SCANNER_CHANNELS:
			wasHandled = processSelectorKnobActionForScannerChannels(action);
			break;
			
		case MODE_INFO:
			wasHandled = processSelectorKnobActionForInfo(action);
			break;

			
		case MODE_CHANNEL_INFO:
			wasHandled = processSelectorKnobActionForChannelInfo(action);
			break;
			
		default:
			break;
	}
	return wasHandled;
}


// MARK: -  Menu Mode

void DisplayMgr::resetMenu() {
	_menuItems.clear();
	_currentMenuItem = 0;
	_menuTimeout = 0;
	_menuTitle = "";
	_menuCB = nullptr;
	
}

void DisplayMgr::showMenuScreen(vector<menuItem_t> items,
										  uint intitialItem,
										  string title,
										  time_t timeout,
										  menuSelectedCallBack_t cb){
	
	//	pthread_mutex_lock (&_mutex);
	
	resetMenu();
	_menuItems = items;
	_menuTitle = title;
	_currentMenuItem =  min( max( static_cast<int>(intitialItem), 0),static_cast<int>( _menuItems.size()) -1);
	_menuCursor	= 0;
	
	_menuTimeout = timeout;
	_menuCB = cb;
	
	//	pthread_mutex_unlock (&_mutex);
	setEvent(EVT_PUSH,MODE_MENU);
}

void  DisplayMgr::updateMenuItems(vector<menuItem_t> items){
	if(_current_mode == MODE_MENU) {
		_menuItems = items;
		
	}
}

bool DisplayMgr::menuSelectAction(knob_action_t action){
	bool wasHandled = false;
	
	if(_current_mode == MODE_MENU) {
		wasHandled = true;
		
		switch(action){
				
			case KNOB_EXIT:
				if(_menuCB) {
					_menuCB(false, 0, action);
				}
				setEvent(EVT_POP, MODE_UNKNOWN);
				resetMenu();
				break;
				
			case KNOB_UP:
				_currentMenuItem = min(_currentMenuItem + 1,  static_cast<int>( _menuItems.size() -1));
				setEvent(EVT_NONE,MODE_MENU);
				break;
				
			case KNOB_DOWN:
				_currentMenuItem = max( _currentMenuItem - 1,  static_cast<int>(0));
				setEvent(EVT_NONE,MODE_MENU);
				break;
				
			case KNOB_DOUBLE_CLICK:
			{
				
				// save the vars that get reset
				auto cb = _menuCB;
				auto item = _currentMenuItem;
				
				if(cb) {
					cb(true,  item, KNOB_DOUBLE_CLICK);
				}
				// force redraw
				drawMenuScreen(TRANS_ENTERING);
			}
				break;
				
			case KNOB_CLICK:
			{
				// ignore menu separators
				if(_menuItems[_currentMenuItem] == "-")
					break;
				
				// need to pop first // menu might force another menu
				
				// save the vars that get reset
				auto cb = _menuCB;
				auto item = _currentMenuItem;
				
				pthread_mutex_lock (&_mutex);
				resetMenu();
				
				//  if you actually selected a menu, then just pop the mode..
				//  you dont have to give it a TRANS_LEAVING
				
				drawMenuScreen(TRANS_LEAVING);  // force menu exit
				popMode();			// remove the menu
				popMode();	// do it twice.. remove the old mode.
				pthread_mutex_unlock (&_mutex);
				
				if(cb) {
					cb(true,  item, action);
				}
			}
				break;
				
			default: break;
		}
		
	}
	
	return wasHandled;
}


void DisplayMgr::drawMenuScreen(modeTransition_t transition){
	
	//	uint8_t width = _vfd.width();
	uint8_t height = _vfd.height();
	
	uint8_t startV =  24;
	uint8_t lineHeight = 9;
	uint8_t maxLines =  (height - startV) / lineHeight ;  // this starts at 0
	//	uint8_t maxCol = width / 7;
	
	if(transition == TRANS_LEAVING) {
		_rightKnob.setAntiBounce(antiBounceDefault);
		_vfd.clearScreen();
		return;
	}
	
	if(transition == TRANS_ENTERING) {
		_rightKnob.setAntiBounce(antiBounceSlow);
		_vfd.clearScreen();
		auto title = _menuTitle;
		
		if(title.size() > 16){
			std::transform(title.begin(), title.end(),title.begin(), ::toupper);
			title = truncate(title, 20);
			_vfd.setFont(VFD::FONT_MINI);
			_vfd.setCursor(10,10);
		}
		else {
			if (title.empty()) title = "Select";
			_vfd.setFont(VFD::FONT_5x7);
			_vfd.setCursor(20,10);
		}
		_vfd.write(title);
	}
	
	// did something change?
	if(transition == TRANS_ENTERING || transition == TRANS_REFRESH){
		
		_vfd.setFont(VFD::FONT_5x7);
		if( (_currentMenuItem - maxLines) > _menuCursor) {
			_menuCursor = max(_currentMenuItem - maxLines, 0);
		}
		else if(_currentMenuItem < _menuCursor) {
			_menuCursor = max(_menuCursor - 1,  0);
		}
		
		uint8_t cursorV = startV;
		for(int i = _menuCursor; (i <= _menuCursor + maxLines) && (i < _menuItems.size()) ; i ++){
			char buffer[64] = {0};
			_vfd.setCursor(0,cursorV);
			//			sprintf(buffer, "%c%-18s %s",  i == _currentMenuItem?'\xb9':' ' , _menuItems[i].c_str(), moreIndicator.c_str());
			sprintf(buffer, "%c%-18s",  i == _currentMenuItem?'\xb9':' ' , _menuItems[i].c_str() );
			_vfd.write(buffer );
			cursorV += lineHeight;
		}
		
		if(_menuItems.size() >  maxLines+1) {
			uint8_t scrolltop = startV-lineHeight;
			float bar_height =  (float)(maxLines +1)/ (float)_menuItems.size() ;
			float offset =  (float)_currentMenuItem / ((float)_menuItems.size() -1) ;
			_vfd.drawScrollBar(scrolltop, bar_height ,offset);
		}
	}
	
}

// MARK: -  mode utils

bool DisplayMgr::isStickyMode(mode_state_t md){
	bool isSticky = false;
	
	switch(md){
		case MODE_TIME:
		case MODE_RADIO:
		case MODE_SCANNER:
		case MODE_GPS:
		case MODE_GPS_WAYPOINT:
		case MODE_INFO:
		case MODE_CANBUS:
		case MODE_CHANNEL_INFO:
		case MODE_DTC:
			isSticky = true;
			break;
			
		default:
			isSticky = false;
	}
	
	return isSticky;
}


bool DisplayMgr::pushMode(mode_state_t newMode){
	
	bool didChange = false;
	if(_current_mode != newMode){
		if(isStickyMode(_current_mode)){
			_saved_mode = _current_mode;
		}
		_current_mode = newMode;
		
		didChange = true;
		
	}
	return didChange;
}

void  DisplayMgr::popMode(){
	
	auto newMode = _saved_mode==MODE_UNKNOWN ? handleRadioEvent():_saved_mode;
	
	// 	printf("popMode  / c: %d / s: %d / n: %d \n", _current_mode, _saved_mode,newMode);
	
	_current_mode = newMode;
	_saved_mode = MODE_UNKNOWN;
}

// MARK: -  DisplayUpdate thread

void DisplayMgr::DisplayUpdateLoop(){
	
	//	printf("start DisplayUpdate\n");
	PRINT_CLASS_TID;
	
	pthread_condattr_t attr;
	pthread_condattr_init( &attr);
#if !defined(__APPLE__)
	//pthread_condattr_setclock is not supported on macOS
	pthread_condattr_setclock( &attr, TIMEDWAIT_CLOCK);
#endif
	pthread_cond_init( &_cond, &attr);
	
	while(_isRunning){
		
		// if not setup // check back later
		if(!_isSetup){
			sleep(1);
			continue;
		}
		
		//		// --check if any events need processing else wait for a timeout
		pthread_mutex_lock (&_mutex);
		
		// wait for event.
		while(_eventQueue.size() == 0){
			
			// delay for a bit
			struct timespec ts = {0, 0};
			struct timespec now = {0, 0};
			clock_gettime(TIMEDWAIT_CLOCK, &now);
			ts = timespec_add(now, timespec_from_ms(1000));
			
			// wait for _eventQueue or time delay == ETIMEDOUT
			int result = pthread_cond_timedwait(&_cond, &_mutex, &ts);
			if(result){
				if(result != ETIMEDOUT){
					printf( "DisplayUpdateLoop: pthread_cond_timedwait : %s\n", strerror(result));
				}
				
#if 0
				// debugging how pthread_cond_timedwait works
				struct timespec ts1 = {0, 0};
				clock_gettime(TIMEDWAIT_CLOCK, &ts1);
				printf("DisplayUpdateLoop:: pthread_cond_timedwait delay = %ld\n",
						 timespec_to_ms(timespec_sub(ts1, now)));
				
#endif
				
				break;
			}
		}
		
		eventQueueItem_t item = {EVT_NONE,MODE_UNKNOWN};
		if(_eventQueue.size()){
			item = _eventQueue.front();
			_eventQueue.pop();
		}
		mode_state_t lastMode = _current_mode;
		
		pthread_mutex_unlock (&_mutex);
		
		//		if(!_isRunning || !_isSetup)
		//			continue;
		
		bool shouldRedraw = false;			// needs complete redraw
		bool shouldUpdate = false;			// needs update of data
		string eventArg = "";
		
		switch(item.evt){
				
				// timeout - nothing happened
			case EVT_NONE:
				struct timespec now, diff;
				clock_gettime(CLOCK_MONOTONIC, &now);
				diff = timespec_sub(now, _lastEventTime);
				
				// check for startup timeout delay
				if(_current_mode == MODE_STARTUP) {
					
					shouldRedraw = false;
					shouldUpdate = true;
					
					if(diff.tv_sec >=  3) {
						pushMode(MODE_TIME);
						shouldRedraw = true;
						shouldUpdate = true;
					}
				}
				else if(_current_mode == MODE_DEV_STATUS) {
					if(diff.tv_sec >=  2) {
						pushMode(MODE_TIME);
						shouldRedraw = true;
						shouldUpdate = true;
					}
				}
				else if(_current_mode == MODE_DTC) {
					// check for {EVT_NONE,MODE_DTC}  which is a scroll change
					if(item.mode == MODE_DTC) {
						shouldRedraw = false;
						shouldUpdate = true;
					}
				}
				
				else if(_current_mode == MODE_GPS_WAYPOINTS) {
					// check for {EVT_NONE,MODE_GPS_WAYPOINTS}  which is a scroll change
					if(item.mode == MODE_GPS_WAYPOINTS) {
						clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);
						shouldRedraw = false;
						shouldUpdate = true;
					}
					// check for  timeout delay
					else if(_menuTimeout > 0 && diff.tv_sec >= _menuTimeout){
						// timeout pop mode?
						popMode();
						shouldRedraw = true;
						shouldUpdate = true;
					}
				}
				else if(_current_mode == MODE_SCANNER_CHANNELS) {
					// check for {EVT_NONE,MODE_SCANNER_CHANNELS}  which is a scroll change
					if(item.mode == MODE_SCANNER_CHANNELS) {
						clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);
						shouldRedraw = false;
						shouldUpdate = true;
					}
					// check for  timeout delay
					else if(_menuTimeout > 0 && diff.tv_sec >= _menuTimeout){
						// timeout pop mode?
						
						auto savedCB = _scannnerChannelsCB;
						_scannnerChannelsCB = NULL;
						shouldRedraw = false;
						shouldUpdate = false;
						//
						if(savedCB) {
							savedCB(false, {RadioMgr::MODE_UNKNOWN, 0}, KNOB_EXIT);
						}
						
						popMode();
						shouldRedraw = true;
						shouldUpdate = true;
					}
				}
				else if(_current_mode == MODE_MESSAGE) {
					if(item.mode == MODE_MESSAGE) {
						clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);
						shouldRedraw = false;
						shouldUpdate = true;
					}
					// check for  timeout delay
					else if(_menuTimeout > 0 && diff.tv_sec >= _menuTimeout){
						// timeout pop mode?
						auto savedCB = _simpleCB;
						//					popMode();
						_knobCB = NULL;
						shouldRedraw = false;
						shouldUpdate = false;
						//
						if(savedCB) {
							savedCB();
						}
					}
				}
				else if(_current_mode == MODE_SLIDER) {
					
					auto * cb = _menuSliderCBInfo;
					if(cb){
						// check for {EVT_NONE,MODE_SLIDER}  which is a slider change
						if(item.mode == MODE_SLIDER) {
							clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);;
							shouldRedraw = false;
							shouldUpdate = true;
						}
						else if(diff.tv_sec >=  cb->timeout){
							// timeout pop mode?
							popMode();
							shouldRedraw = true;
							shouldUpdate = true;
						}
					}
				}

				else if(_current_mode == MODE_SELECT_SLIDER) {
					
					auto * cb = _menuSelectionSliderCBInfo;
					if(cb){
						// check for {EVT_NONE,MODE_SLIDER}  which is a slider change
						if(item.mode == MODE_SELECT_SLIDER) {
							clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);;
							shouldRedraw = false;
							shouldUpdate = true;
						}
						else if(diff.tv_sec >=  cb->timeout){
							// timeout pop mode?
							popMode();
							shouldRedraw = true;
							shouldUpdate = true;
						}
					}
				}
 
				else if(_current_mode == MODE_SQUELCH) {
					
					// check for {EVT_NONE,MODE_SQUELCH}  which is a squelch change
					if(item.mode == MODE_SQUELCH) {
						clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);;
						shouldRedraw = false;
						shouldUpdate = true;
					}
					else if(diff.tv_sec >=  5){
						// timeout pop mode?
						popMode();
						shouldRedraw = true;
						shouldUpdate = true;
					}
				}
				
				else if(_current_mode == MODE_DIMMER) {
					
					// check for {EVT_NONE,MODE_DIMMER}  which is a dimmer change
					if(item.mode == MODE_DIMMER) {
						clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);;
						shouldRedraw = false;
						shouldUpdate = true;
					}
					else if(diff.tv_sec >=  5){
						// timeout pop mode?
						popMode();
						shouldRedraw = true;
						shouldUpdate = true;
					}
				}
				
				else if(_current_mode == MODE_SCANNER) {
					
					// check for {EVT_NONE,MODE_SCANNER}  which is a squelch change
					if(item.mode == MODE_SCANNER) {
						shouldRedraw = false;
						shouldUpdate = true;
					}
				}
				
				else if(_current_mode == MODE_RADIO) {
					
					// check for {EVT_NONE,MODE_RADIO}  which is a airplay change
					if(item.mode == MODE_RADIO) {
						shouldRedraw = false;
						shouldUpdate = true;
					}
				}
				
				else if(_current_mode == MODE_MENU) {
					
					// check for {EVT_NONE,MODE_MENU}  which is a menu change
					if(item.mode == MODE_MENU) {
						clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);;
						shouldRedraw = false;
						shouldUpdate = true;
					}
					// check for menu timeout delay
					else if(_menuTimeout > 0 && diff.tv_sec >= _menuTimeout){
						if(!isStickyMode(_current_mode)){
							popMode();
							
							if(_menuCB) {
								_menuCB(false, 0, KNOB_EXIT);
							}
							resetMenu();
							shouldRedraw = true;
							shouldUpdate = true;
						}
					}
				}
				else if(_current_mode == MODE_DTC_INFO) {
					
					// check for {EVT_NONE,MODE_DTC_INFO}  which is a click
					if(item.mode == MODE_DTC_INFO) {
						clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);;
						shouldRedraw = false;
						shouldUpdate = true;
					}
					// give it 10 seconds
					else if(diff.tv_sec >=  10){
						// timeout pop mode?
						popMode();
						shouldRedraw = true;
						shouldUpdate = true;
					}
				}
				else if(_current_mode == MODE_INFO) {
					
					// check for {EVT_NONE,MODE_INFO}  which is a click
					if(item.mode == MODE_DTC_INFO) {
						clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);;
						shouldRedraw = false;
						shouldUpdate = true;
					}
					// give it 10 seconds
					else if(_menuTimeout > 0 && diff.tv_sec >= _menuTimeout){
								// timeout pop mode?
						popMode();
						shouldRedraw = true;
						shouldUpdate = true;
					}
				}
 				else if(_current_mode == MODE_EDIT_STRING) {
					
					// check for {EVT_NONE,MODE_EDIT_STRING}  which is a click
					if(item.mode == MODE_EDIT_STRING) {
						clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);;
						shouldRedraw = false;
						shouldUpdate = true;
					}
					//					// give it 10 seconds
					//					else if(diff.tv_sec >=  10){
					//						// timeout pop mode?
					//						popMode();
					//						shouldRedraw = true;
					//						shouldUpdate = true;
					//					}
					//				}
				}
				
				// check for ay other timeout delay 1.3 secs
				else if(diff.tv_sec >=  1){
					// should we pop the mode?
					if(!isStickyMode(_current_mode)){
						popMode();
						shouldRedraw = true;
						shouldUpdate = true;
					}
				}
				break;
				
			case EVT_PUSH:
				//			printf("\nEVT_PUSH %d \n", item.mode);
				
				if(pushMode(item.mode)){
					shouldRedraw = true;
					eventArg = item.arg;
				}
				clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);;
				shouldUpdate = true;
				break;
				
			case EVT_POP:
				if(!isStickyMode(_current_mode)){
					popMode();
					shouldRedraw = true;
					shouldUpdate = true;
				}
				break;
				
			case EVT_REDRAW:
				clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);;
				shouldRedraw = true;
				//				shouldUpdate = true;
				break;
		}
		
		if(lastMode != _current_mode)
			drawMode(TRANS_LEAVING, lastMode );
		
		if(shouldRedraw)
			drawMode(TRANS_ENTERING, _current_mode, eventArg );
		else if(shouldUpdate)
			drawMode(TRANS_REFRESH, _current_mode );
		else
			drawMode(TRANS_IDLE, _current_mode );
	}
}


DisplayMgr::mode_state_t DisplayMgr::handleRadioEvent(){
	mode_state_t newState = MODE_UNKNOWN;
	
	//	PiCarDB*	db 	= PiCarMgr::shared()->db();
	RadioMgr*	radio 	= PiCarMgr::shared()->radio();
	
	if(radio->isOn()){
		newState = radio->isScannerMode()?MODE_SCANNER:MODE_RADIO;
	}else {
		newState = MODE_TIME;
	}
	
	return newState;
}


void* DisplayMgr::DisplayUpdateThread(void *context){
	DisplayMgr* d = (DisplayMgr*)context;
	
	//   the pthread_cleanup_push needs to be balanced with pthread_cleanup_pop
	pthread_cleanup_push(   &DisplayMgr::DisplayUpdateThreadCleanup ,context);
	
	d->DisplayUpdateLoop();
	
	pthread_exit(NULL);
	
	pthread_cleanup_pop(0);
	return((void *)1);
}


void DisplayMgr::DisplayUpdateThreadCleanup(void *context){
	//	DisplayMgr* d = (DisplayMgr*)context;
	
	//	printf("cleanup display\n");
}

// MARK: -  Display Draw code


void DisplayMgr::drawMode(modeTransition_t transition,
								  mode_state_t mode,
								  string eventArg  ){
	
	if(!_isSetup)
		return;
	//	//
	//		vector<string> l1 = { "ENT","RFR","IDL","XIT"};
	//		if(transition != TRANS_IDLE)
	//			printf("drawMode %s %d\n", l1[transition].c_str(),  mode);
	//
	try {
		switch (mode) {
				
			case MODE_STARTUP:
				drawStartupScreen(transition);
				break;
				
			case MODE_SLIDER:
				drawSliderScreen(transition);
				break;

			case MODE_SELECT_SLIDER:
				drawSelectSliderScreen(transition);
				break;

			case MODE_DEV_STATUS:
				drawDeviceStatusScreen(transition);
				break;
				
			case MODE_TIME:
				drawTimeScreen(transition);
				break;
				
			case MODE_DIMMER:
				drawDimmerScreen(transition);
				break;
				
			case MODE_SQUELCH:
				drawSquelchScreen(transition);
				break;
				
			case MODE_RADIO:
				drawRadioScreen(transition);
				break;
				
			case MODE_SCANNER:
				drawScannerScreen(transition);
				break;
				
			case MODE_MENU:
				drawMenuScreen(transition);
				break;
				
			case MODE_GPS:
				drawGPSScreen(transition);
				break;
				
			case MODE_GPS_WAYPOINTS:
				drawGPSWaypointsScreen(transition);
				break;
				
			case MODE_GPS_WAYPOINT:
				drawGPSWaypointScreen(transition);
				break;
				
			case MODE_MESSAGE:
				drawMessageScreen(transition);
				break;
				
			case MODE_SCANNER_CHANNELS:
				drawScannerChannels(transition);
				break;
				
			case MODE_CHANNEL_INFO:
				drawChannelInfo(transition);
				break;
				
			case MODE_DTC:
				drawDTCScreen(transition);
				break;
				
			case MODE_DTC_INFO:
				drawDTCInfoScreen(transition, eventArg);
				break;
				
			case MODE_CANBUS:
				if(_currentPage == 0)
					drawCANBusScreen(transition);
				else
					drawCANBusScreen1(transition);
				break;
				
			case MODE_EDIT_STRING:
				drawEditStringScreen(transition);
				break;
				
			case MODE_INFO:
				drawInfoScreen(transition);
				break;
				
			case MODE_UNKNOWN:
				// we will always leave the UNKNOWN state at start
				if(transition == TRANS_LEAVING)
					break;
				
			default:
				drawInternalError(transition);
		}
		
	}
	catch ( const Exception& e)  {
		printf("\tError %d %s\n\n", e.getErrorNumber(), e.what());
		
	}
	catch (std::invalid_argument& e)
	{
		printf("EXCEPTION: %s ",e.what() );
		
	}
}


#if 0
// for debugging
static void dumpHex(uint8_t* buffer, size_t length, int offset)
{
	char hexDigit[] = "0123456789ABCDEF";
	size_t			i;
	size_t						lineStart;
	size_t						lineLength;
	short					c;
	const unsigned char	  *bufferPtr = buffer;
	
	char                    lineBuf[1024];
	char                    *p;
	
#define kLineSize	8
	for (lineStart = 0, p = lineBuf; lineStart < length; lineStart += lineLength,  p = lineBuf )
	{
		lineLength = kLineSize;
		if (lineStart + lineLength > length)
			lineLength = length - lineStart;
		
		p += sprintf(p, "%6lu: ", lineStart+offset);
		for (i = 0; i < lineLength; i++){
			*p++ = hexDigit[ bufferPtr[lineStart+i] >>4];
			*p++ = hexDigit[ bufferPtr[lineStart+i] &0xF];
			if((lineStart+i) &0x01)  *p++ = ' ';  ;
		}
		for (; i < kLineSize; i++)
			p += sprintf(p, "   ");
		
		p += sprintf(p,"  ");
		for (i = 0; i < lineLength; i++) {
			c = bufferPtr[lineStart + i] & 0xFF;
			if (c > ' ' && c < '~')
				*p++ = c ;
			else {
				*p++ = '.';
			}
		}
		*p++ = 0;
		
		
		printf("%s\n",lineBuf);
	}
#undef kLineSize
}

#endif
// MARK: -  drawRadioScreen

void DisplayMgr::drawRadioScreen(modeTransition_t transition){
	
	//	printf("drawRadioScreen  %d\n",transition);
	
	
	PiCarMgr* mgr	= PiCarMgr::shared();
	RadioMgr* radio 	= PiCarMgr::shared()->radio();
	
	int centerX = _vfd.width() /2;
	int centerY = _vfd.height() /2;
	
	static int  modeStart = 5;
	
	static RadioMgr::radio_mode_t lastMode = RadioMgr::MODE_UNKNOWN;
	
	RadioMgr::radio_mode_t  mode  = radio->radioMode();
	RadioMgr::radio_mux_t 	mux  =  radio->radioMuxMode();
	string muxstring = RadioMgr::muxstring(mux);
	
	bool forceRefresh = false;
	
	//	printf("display RadioScreen %s %s %d |%s| \n",redraw?"REDRAW":"", shouldUpdate?"UPDATE":"" ,
	//			 radio->radioMuxMode(),
	//			 	RadioMgr::muxstring(radio->radioMuxMode()).c_str() );
	
	if(transition == TRANS_LEAVING) {
		_rightRing.clearAll();
		return;
	}
	
	if(transition == TRANS_ENTERING) {
		_vfd.clearScreen();
		_rightRing.clearAll();
		
	}
	
	if(transition == TRANS_IDLE) {
		_rightRing.clearAll();
	}
	
	
	// avoid doing a needless refresh.  if this was a timeout event,  then just update the time
	if(transition == TRANS_ENTERING || transition == TRANS_REFRESH || forceRefresh) {
		
		if(! radio->isOn()){
			string str = "OFF";
			auto textCenter =  centerX - (str.size() * 11);
			
			TRY(_vfd.setFont(VFD::FONT_10x14));
			TRY(_vfd.setCursor( textCenter ,centerY+10));
			TRY(_vfd.write(str));
		}
		else {
			uint32_t  freq =  radio->frequency();
			// we might need an extra refresh if switching modes
			if(lastMode != mode){
				_vfd.clearScreen();
				_rightRing.clearAll();
				lastMode = mode;
			}
			
			if(mode == RadioMgr::AUX){
				
				string str = "AUX";
				auto freqCenter =  centerX  -( (str.size() /2)  * 11) - 7 ;
				
				TRY(_vfd.setFont(VFD::FONT_10x14));
				TRY(_vfd.setCursor( freqCenter ,centerY+10));
				TRY(_vfd.write(str));
			}
			else if(mode == RadioMgr::AIRPLAY){
				
				_vfd.setFont(VFD::FONT_5x7);
				
				constexpr int maxLen = 21;
				string spaces(maxLen, ' ');
				
				string titleStr = "";
				string artistStr = "";
				
				uint8_t lastAirplayStatus = 0;
				
				// get artist and title
				pthread_mutex_lock (&_apmetadata_mutex);
				
				lastAirplayStatus = _airplayStatus;
				
				if(_airplayMetaData.count("asar")){
					artistStr = Utils::trim(_airplayMetaData["asar"]);
				}
				if(_airplayMetaData.count("minm")){
					titleStr = Utils::trim(_airplayMetaData["minm"]);
				}
				pthread_mutex_unlock(&_apmetadata_mutex);
				
				if(lastAirplayStatus == 0){
					
					uint8_t buff2[] = {
						VFD::VFD_CLEAR_AREA,
						static_cast<uint8_t>(0),  static_cast<uint8_t> (centerY-16),
						static_cast<uint8_t> (128),static_cast<uint8_t> (centerY+4)};
					_vfd.writePacket(buff2, sizeof(buff2));
					
					string str = "- NOT PLAYING -";
					_vfd.setCursor( centerX - ((str.size()*5) /2 ), centerY-7);
					_vfd.setFont(VFD::FONT_MINI);
					_vfd.printPacket(str.c_str() );
				}
				else {
					// correct UTF8 single comma quotation mark apostrophe
					titleStr = Utils:: removeDiacritics(titleStr);
					// remove parenthetical text  regex (\()(?:[^\)\\]*(?:\\.)?)*\)
					titleStr = regex_replace(titleStr, regex("(\\()(?:[^\\)\\\\]*(?:\\\\.)?)*\\)"), "");
					
					// center it
					titleStr = truncate(titleStr, maxLen);
					string portionOfSpaces = spaces.substr(0, (maxLen - titleStr.size()) / 2);
					titleStr = portionOfSpaces + titleStr;
					
					artistStr = Utils:: removeDiacritics(artistStr);
					
					artistStr = truncate(artistStr, maxLen);
					string portionOfSpaces1 = spaces.substr(0, (maxLen - artistStr.size()) / 2);
					artistStr = portionOfSpaces1 + artistStr;
					
					_vfd.setFont(VFD::FONT_5x7);
					
					_vfd.setCursor(0,centerY-7);
					_vfd.printPacket("%-21s",titleStr.c_str() );
					
					_vfd.setCursor(0,centerY+3);
					_vfd.printPacket("%-21s",artistStr.c_str() );
					
				}
				
				drawAirplayLogo(0, centerY+9, radio->hasAirplay()?"":" :OFF");
			}
			else {
				
				int precision = 0;
				
				switch (mode) {
					case RadioMgr::BROADCAST_AM: precision = 0;break;
					case RadioMgr::BROADCAST_FM: precision = 1;break;
					default :
						precision = 3; break;
				}
				
				string str = 	RadioMgr::hertz_to_string(freq, precision);
				string hzstr =	RadioMgr::freqSuffixString(freq);
				string modStr = RadioMgr::modeString(mode);
				
				auto freqCenter =  centerX - (str.size() * 11) + 18;
				if(precision > 1)  freqCenter += 10*2;
				
				modeStart = 5;
				if(precision == 0)
					modeStart += 15;
				else if  (precision == 1)
					modeStart += 5;
				
				_vfd.setFont(VFD::FONT_MINI);
				_vfd.setCursor(modeStart, centerY+0) ;
				_vfd.write(modStr);
				
				_vfd.setFont(VFD::FONT_10x14);
				_vfd.setCursor( freqCenter ,centerY+10);
				_vfd.write(str);
				
				_vfd.setFont(VFD::FONT_MINI); _vfd.write( " ");
				_vfd.setFont(VFD::FONT_5x7); _vfd.write( hzstr);
				
				// Draw title centered inb char buffer
				constexpr int  titleMaxSize = 20;
				char titlebuff[titleMaxSize + 1];
				memset(titlebuff,' ', titleMaxSize);
				titlebuff[titleMaxSize] = '\0';
				int titleStart =  centerX - ((titleMaxSize * 6)/2);
				int titleBottom = centerY -10;
				PiCarMgr::station_info_t info;
				if(mgr->getStationInfo(mode, freq, info)){
					string title = truncate(info.title, titleMaxSize);
					int titleLen = (int)title.size();
					int offset  = (titleMaxSize /2) - (titleLen/2);
					memcpy( titlebuff+offset , title.c_str(), titleLen );
				};
				TRY(_vfd.setCursor( titleStart ,titleBottom ));
				TRY(_vfd.write( titlebuff));
				
			}
			_vfd.setCursor(0, 60);
			if(mgr->isPresetChannel(mode, freq)){
				_vfd.setFont(VFD::FONT_MINI);
				_vfd.printPacket("PRESET");
			}
			else {
				_vfd.printPacket("      ");
			}
		}
	}
	
	if(radio->isOn()
		&& ( mode == RadioMgr::BROADCAST_FM
			 ||  mode == RadioMgr::VHF
			 ||  mode == RadioMgr::GMRS	))
	{
		_vfd.setFont(VFD::FONT_MINI);
		_vfd.setCursor(modeStart, centerY+9);
		_vfd.write(muxstring);
		
		drawReceptionBars(0, centerY+19, radio->get_if_level());
		_vfd.printPacket("%-8s", radio->isSquelched()?"SQLCH":"" );
	}
	
	drawEngineCheck();
	drawTemperature();
	drawTimeBox();
}

// MARK: -  other draws Screens



void DisplayMgr::drawMessageScreen(modeTransition_t transition){
	
	
	if(transition == TRANS_ENTERING) {
		_vfd.clearScreen();
		
		uint8_t width = _vfd.width();
		
		int centerX = width /2;
		int centerY = _vfd.height() /2;
		
		string str = _menuTitle;
		
		if(str.size() > 20){
			std::transform(str.begin(), str.end(),str.begin(), ::toupper);
			str = truncate(str, 40);
			_vfd.setCursor( centerX - ((str.size()*5) /2 ), centerY + 5);
			_vfd.setFont(VFD::FONT_MINI);
		}
		else{
			_vfd.setCursor( centerX - ((str.size()*7) /2 ), centerY + 5);
			_vfd.setFont(VFD::FONT_5x7);
		}
		
		_vfd.printPacket("%s", str.c_str());
	}
	
	if(transition == TRANS_LEAVING) {
		return;
	}
	
	drawTimeBox();
}



void DisplayMgr::drawStartupScreen(modeTransition_t transition){
	
	
#if defined(__APPLE__)
	if(transition == TRANS_ENTERING){
		printf("Enter drawStartupScreen\n");
	}
	
	if(transition == TRANS_LEAVING){
		printf("Leaving drawStartupScreen\n");
		
	}
#else
	uint8_t width = _vfd.width();
	uint8_t height = _vfd.height();
	
	int centerX = width /2;
	int centerY = _vfd.height() /2;
	
	
	if(transition == TRANS_ENTERING){
		
		_vfd.setPowerOn(true);
		_vfd.clearScreen();
		_vfd.clearScreen();
		uint8_t leftbox 	= 5;
		uint8_t rightbox 	= width - 5;
		uint8_t topbox 	= 5 ;
		uint8_t bottombox = height - 5  ;
		
		_vfd.clearScreen();
		
		//draw box outline
		uint8_t buff1[] = {VFD::VFD_OUTLINE,leftbox,topbox,rightbox,bottombox };
		_vfd.writePacket(buff1, sizeof(buff1), 0);
		
		string str = "PiCar";
		auto start  =  centerX  -( (str.size() /2)  * 11) - 7 ;
		_vfd.setFont(VFD::FONT_10x14);
		_vfd.setCursor( start ,centerY+5);
		_vfd.write(str);
		
		string verstr = string(PiCarMgr::PiCarMgr_Version);
		std::transform(verstr.begin(), verstr.end(),verstr.begin(), ::toupper);
		_vfd.setFont(VFD::FONT_MINI);
		_vfd.printPacket("%s", verstr.c_str());
		
		LEDeventStartup();
	}
	
	if(transition == TRANS_ENTERING || transition == TRANS_REFRESH){
		PiCarMgr*			mgr 	= PiCarMgr::shared();
		RadioMgr*			radio 	= mgr->radio();
		GPSmgr*				gps 		= mgr->gps();
		PiCarCAN*			can 		= mgr->can();
		
		string str = "";
		
		if(radio->isConnected()){
			str += "RADIO ";
		}
		
		if(mgr->isAirPlayRunning()){
			str += " AIRPLAY ";
		}
		
		if(gps->isConnected()){
			str += " GPS ";
		}
		
		if(can->isConnected()){
			str += " CANBUS";
		}
		
		_vfd.setCursor( 15, 55);
		_vfd.setFont(VFD::FONT_MINI);
		_vfd.printPacket("%-20s", str.c_str());
	}
	
	if(transition == TRANS_LEAVING){
	}
	
	//	if(transition == TRANS_ENTERING || transition == TRANS_REFRESH){
	//		drawDeviceStatus();
	//
	//	}
	//	printf("displayStartupScreen %s\n",redraw?"REDRAW":"");
	
	
#endif
}

void DisplayMgr::drawDeviceStatusScreen(modeTransition_t transition){
	
	if(transition == TRANS_ENTERING){
		
		_vfd.clearScreen();
		
		_vfd.setCursor(7, 10);
		_vfd.setFont(VFD::FONT_5x7);
		_vfd.write("Device Status");
		
		drawDeviceStatus();
	}
	
	//	printf("displayStartupScreen %s\n",redraw?"REDRAW":"");
}


void DisplayMgr::drawDeviceStatus(){
	
	char buffer[30];
	uint8_t col = 10;
	uint8_t row = 10;
	
	RadioMgr*	radio 	= PiCarMgr::shared()->radio();
	GPSmgr*	gps 		= PiCarMgr::shared()->gps();
	
	row+=2;
	
	memset(buffer, ' ', sizeof(buffer));
	row+=8; _vfd.setCursor(col, row);
	_vfd.setFont(VFD::FONT_MINI);
	
	RtlSdr::device_info_t info;
	
	if(radio->isConnected() && radio->getDeviceInfo(info) ){
		sprintf( buffer ,"\xBA RADIO OK");
		_vfd.writePacket( (const uint8_t*) buffer,21);
		row += 6;  _vfd.setCursor(col+10, row );
		std::transform(info.product.begin(), info.product.end(),info.product.begin(), ::toupper);
		_vfd.write(info.product);
	}
	else {
		sprintf( buffer ,"X RADIO FAIL");
		_vfd.writePacket( (const uint8_t*) buffer,21);
	}
	
	memset(buffer, ' ', sizeof(buffer));
	row += 8; _vfd.setCursor(col, row);
	if(gps->isConnected()){
		sprintf( buffer ,"\xBA GPS OK");
		_vfd.writePacket( (const uint8_t*) buffer,21);
	}
	else {
		sprintf( buffer ,"X GPS FAIL");
		_vfd.writePacket( (const uint8_t*) buffer,21);
	}
	
	drawTemperature();
	
}



void DisplayMgr::drawTimeScreen(modeTransition_t transition){
	
	int centerY = _vfd.height() /2;
	
	time_t rawtime;
	struct tm timeinfo = {0};
	
	time(&rawtime);
	localtime_r(&rawtime, &timeinfo); // fills in your structure,
	// instead of returning a pointer to a static
	
	char buffer[128] = {0};
	
	if(transition == TRANS_LEAVING) {
		return;
	}
	
	if(transition == TRANS_ENTERING){
		_vfd.clearScreen();
	}
	
	if(rawtime != -1){
		std::strftime(buffer, sizeof(buffer)-1, "%2l:%M:%S", &timeinfo);
		
		_vfd.setCursor(10,38) ;
		_vfd.setFont(VFD::FONT_10x14) ;
		_vfd.write(buffer) ;
		
		_vfd.setFont(VFD::FONT_5x7) ;
		_vfd.write( (timeinfo.tm_hour > 12)?" PM":" AM");
	}
	
	if(_airplayStatus)
		drawAirplayLogo(0, 55);
	else
	{
		uint8_t buff2[] = {
			VFD::VFD_CLEAR_AREA,
			static_cast<uint8_t>(0),  static_cast<uint8_t> (centerY+9),
			static_cast<uint8_t> (0+100),static_cast<uint8_t> (centerY+19)};
		
		_vfd.writePacket(buff2, sizeof(buff2));
	}
	
	drawTemperature();
	drawEngineCheck();
}

void DisplayMgr::drawAirplayLogo(uint8_t x,  uint8_t y, string text ){
	
	const uint8_t airplayLogo[] = {0x1A,0x80, 0x18,0x09, 0x1c,0x62,0x8d,0x93,0xa7,0x93,0x8d,0x62,0x1c};
	
	_vfd.setCursor( x, y)	;
	_vfd.writePacket(airplayLogo, sizeof(airplayLogo));
	
	_vfd.setFont(VFD::FONT_MINI);
	_vfd.setCursor(x+11, y+7);
	_vfd.printPacket("AIRPLAY %-15s",  text.c_str());
}


void  DisplayMgr::drawTemperature(){
	
	PiCarDB*		db 	= PiCarMgr::shared()->db();
	char buffer[128] = {0};
	char* p = &buffer[0];
	
	
	bool hasInside = false;
	bool hasOutside = false;
	
	float fOutside = 0;
	float fInside = 0;
	
	float cTemp = 0;
	if(db->getFloatValue(VAL_OUTSIDE_TEMP, cTemp)){				// GET THIS FROM SOMEWHERE!!!
		fOutside = cTemp *9.0/5.0 + 32.0;
		hasOutside = true;
	}
	if(db->getFloatValue(VAL_INSIDE_TEMP, cTemp)){				// GET THIS FROM SOMEWHERE!!!
		fInside = cTemp *9.0/5.0 + 32.0;
		hasInside = true;
	}
	
	if(hasInside){
		p+=  sprintf(p, "%d\xa0%s", (int) round(fInside) ,  (hasOutside?"":"F") );
	}
	
	if(hasOutside){
		p+=  sprintf(p, "%s%d\xa0" "F",  (hasInside?"/":""), (int) round(fOutside) );
	}
	
	if(hasInside || hasOutside){
		_vfd.setCursor( 0, 7)	;
		_vfd.setFont(VFD::FONT_5x7);
		_vfd.printPacket("%-10s", buffer);
	}
	
	
	//	if(db->getFloatValue(VAL_CPU_INFO_TEMP, cTemp)){
	//		char buffer[64] = {0};
	//
	//		TRY(_vfd.setCursor(64, 55));
	//		TRY(_vfd.setFont(VFD::FONT_5x7));
	//		sprintf(buffer, "CPU:%d\xa0" "C ", (int) round(cTemp) );
	//		TRY(_vfd.write(buffer));
	//	}
	
}


void DisplayMgr::drawEngineCheck(){
	FrameDB*		fDB 	= PiCarMgr::shared()->can()->frameDB();
	uint8_t width = _vfd.width();
	uint8_t midX = width/2;
	
	bool engineCheck = false;
	bitset<8>  bits = {0};
	
	_vfd.setCursor(midX, 60);
	
	char buffer[20] = {0};
	
	if(fDB->boolForKey("GM_CHECK_ENGINE", engineCheck)
		&& engineCheck) {
		sprintf(buffer, "CHECK ENGINE");
	}
	else if(fDB->boolForKey("GM_OIL_LOW", engineCheck)
			  && engineCheck) {
		sprintf(buffer, "OIL LOW");
	}
	else if(fDB->boolForKey("GM_CHANGE_OIL", engineCheck)
			  && engineCheck) {
		sprintf(buffer, "CHANGE OIL");
	}
	else if(fDB->boolForKey("GM_CHECK_FUELCAP", engineCheck)
			  && engineCheck) {
		sprintf(buffer, "CHECK FUEL CAP");
	}
	else if(fDB->bitsForKey("JK_DOORS", bits) && bits.count()){
		if(bits.count() == 1) {
			if( bits.test(4) )sprintf(buffer, "GATE OPEN");
			else sprintf(buffer, "DOOR OPEN");
		}
		else sprintf(buffer, "DOORS OPEN");
	}
	
	_vfd.setFont(VFD::FONT_MINI);
	_vfd.printPacket("%-20s", buffer);
	
	
}


void DisplayMgr::drawInternalError(modeTransition_t transition){
	
	//	printf("displayInternalError  %d\n",transition);
	
	
	if(transition == TRANS_ENTERING) {
		_vfd.clearScreen();
	}
	
	if(transition == TRANS_LEAVING) {
		return;
	}
	
}


void DisplayMgr::drawShutdownScreen(){
	
	
	//	printf("shutdown display");
	_vfd.clearScreen();
	_vfd.clearScreen();
	_rightRing.clearAll();
	_leftRing.clearAll();
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(10,35));
	TRY(_vfd.write("  Well... Bye"));
	sleep(1);
	_vfd.clearScreen();
	
}

void DisplayMgr::drawCANBusScreen(modeTransition_t transition){
	
	PiCarCAN*	can 	= PiCarMgr::shared()->can();
	
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	int64_t nowSecs = timespec_to_ms(now) / 1000;
	
	constexpr int busTimeout = 5;
	
	if(transition == TRANS_ENTERING) {
		_rightKnob.setAntiBounce(antiBounceSlow);
		_vfd.clearScreen();
	}
	
	if(transition == TRANS_LEAVING) {
		_rightKnob.setAntiBounce(antiBounceDefault);
		return;
	}
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(0,18));
	TRY(_vfd.write("CANbus Activity"));
	
	char buffer[64] = {0};
	char* p = buffer;
	
	// GM BUS
	time_t lastTime = 0;
	size_t count = 0;
	if(can->lastFrameTime(PiCarCAN::CAN_GM, lastTime)){
		
		time_t diff = nowSecs - lastTime;
		if(diff < busTimeout ){
			if(can->packetsPerSecond(PiCarCAN::CAN_GM, count)){
				
			}
		}
		else count = 0;
	}
	
	p = buffer;
	p  += sprintf(p, "%4s: ", "GM");
	if(count > 0)
		p  += sprintf(p, "%4zu/sec", count);
	else
		p  += sprintf(p, "%-10s","---");
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(10,33));
	TRY(_vfd.write(buffer));
	
	// JEEP BUS
	lastTime = 0;
	count = 0;
	if(can->lastFrameTime(PiCarCAN::CAN_JEEP, lastTime)){
		time_t diff = nowSecs - lastTime;
		
		if(diff < busTimeout ){
			if(can->packetsPerSecond(PiCarCAN::CAN_JEEP, count)){
				
			}
		}
		else count = 0;
	}
	
	p = buffer;
	p  += sprintf(p, "%4s: ", "Jeep");
	if(count > 0)
		p  += sprintf(p, "%4zu/sec  ", count);
	else
		p  += sprintf(p, "%-10s","---");
	
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(10,43));
	TRY(_vfd.write(buffer));
	
	drawTimeBox();
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(_vfd.width()-5,60));
	_vfd.write(moreNext) ;
	
}


void DisplayMgr::drawCANBusScreen1(modeTransition_t transition){
	
	static map <uint8_t, PiCarDB::canbusdisplay_prop_t> cachedProps;
	
	PiCarCAN*	can 	= PiCarMgr::shared()->can();
	PiCarDB*		db 	= PiCarMgr::shared()->db();
	uint8_t width 	= _vfd.width();
	uint8_t midX = width/2;
	
	uint8_t col1 = 5;
	uint8_t col2 = midX + 5;
	uint8_t row1 = 16;
	uint8_t rowsize = 19;
	
	int start_item = ((_currentPage -1) *6) +1;			// 1-6 for each page
	int end_item	= start_item + 6;
	
	if(transition == TRANS_ENTERING) {
		
		// erase any pollingwe might have cached
		for(auto  e: cachedProps){
			can->cancel_OBDpolling(e.second.key);
		}
		cachedProps.clear();
		db->getCanbusDisplayProps(cachedProps);
		_rightKnob.setAntiBounce(antiBounceSlow);
		_vfd.clearScreen();
		
		// draw titles
		_vfd.setFont(VFD::FONT_MINI);
		
		for(uint8_t	 i = start_item; i < end_item; i++)
			if(cachedProps.count(i)){
				auto item = cachedProps[i];
				
				int line = ((i - 1) % 6);
				
				if(i <  end_item - 3){
					can->request_OBDpolling(item.key);
					_vfd.setCursor(col1, row1 + (line)  * rowsize );
					_vfd.write(item.title);
					
				}
				else {
					can->request_OBDpolling(item.key);
					_vfd.setCursor(col2, row1 + ( (line - 3)  * rowsize ));
					_vfd.write(item.title);
				}
			}
	}
	
	if(transition == TRANS_LEAVING) {
		
		// erase any polling we might have cached
		for(auto  e: cachedProps){
			can->cancel_OBDpolling(e.second.key);
		}
		cachedProps.clear();
		
		_rightKnob.setAntiBounce(antiBounceDefault);
		return;
	}
	
	
	// Draw values
	_vfd.setFont(VFD::FONT_5x7);
	for(uint8_t	 i = start_item; i < end_item; i++){
		
		int line = ((i - 1) % 6);
		
		char buffer[30];
		memset(buffer, ' ', sizeof(buffer));
		
		string val1 = "";
		string val2 = "";
		
		if(cachedProps.count(i))
			normalizeCANvalue(cachedProps[i].key, val1);
		
		if(cachedProps.count(i+3))
			normalizeCANvalue(cachedProps[i+3].key, val2);
		
		// spread across 21 chars
		sprintf( buffer , "%-9s  %-9s", val1.c_str(), val2.c_str());
		_vfd.setCursor(col1 ,(row1 + (line)  * rowsize) + 9);
		_vfd.writePacket( (const uint8_t*) buffer,21);
	}
	
	drawTimeBox();
}



void DisplayMgr::drawGPSScreen(modeTransition_t transition){
	
	uint8_t col = 0;
	uint8_t row = 7;
	string str;
	
	uint8_t width = _vfd.width();
	uint8_t midX = width/2;
	
	uint8_t utmRow = row;
	uint8_t altRow = utmRow+30;
	
	GPSmgr*	gps 	= PiCarMgr::shared()->gps();
	
	if(transition == TRANS_ENTERING) {
		_vfd.clearScreen();
		
		// draw titles
		_vfd.setFont(VFD::FONT_MINI);
		_vfd.setCursor(2,utmRow);
		_vfd.printPacket("UTM");
		
		_vfd.setCursor(2,altRow);
		_vfd.printPacket("ALTITUDE");
		
		_vfd.setCursor(midX +20 ,utmRow+10);
		_vfd.printPacket("HEADING");
		
		_vfd.setCursor(midX +20 ,altRow);
		_vfd.printPacket("SPEED");
		
	}
	
	if(transition == TRANS_LEAVING) {
		return;
	}
	
	
	GPSLocation_t location;
	if(gps->GetLocation(location)){
		string utm = GPSmgr::UTMString(location);
		vector<string> v = split<string>(utm, " ");
		
		_vfd.setFont(VFD::FONT_5x7);
		
		_vfd.setCursor(col+7, utmRow+10 );
		_vfd.printPacket("%-3s", v[0].c_str());
		
		_vfd.setCursor(col+30, utmRow+10 );
		_vfd.printPacket("%-8s", v[1].c_str());
		
		_vfd.setCursor(col+30 - 6, utmRow+20 );
		_vfd.printPacket("%-8s", v[2].c_str());
		
		if(location.altitudeIsValid)  {
			_vfd.setCursor(col+30, altRow+10);
			constexpr double  M2FT = 	3.2808399;
			_vfd.printPacket("%-5.1f",location.altitude * M2FT);
		}
		
		_vfd.setFont(VFD::FONT_MINI);
		_vfd.setCursor(0,60)	;
		_vfd.printPacket( "SATS: %2d ",  location.numSat);
		
		_vfd.setCursor(midX +20,60)	;
		_vfd.printPacket( "DOP: %-2.1f ",  location.DOP/10.);
		
	}
	
	static int	last_heading = INT_MAX;
	_vfd.setFont(VFD::FONT_5x7);
	
	GPSVelocity_t velocity;
	if(gps->GetVelocity(velocity)){
		char buffer[8];
		
		//		printf("3  %f mph %f deg\n",  velocity.speed * 1.150779 , velocity.heading);
		
		//save heading
		last_heading  = int(velocity.heading);
		
		memset(buffer, ' ', sizeof(buffer));
		double mph = velocity.speed * 1.15078;  // knots to mph
		sprintf( buffer , "%3d mph", (int)floor(mph));
		_vfd.setCursor(midX +20 ,altRow+10);
		_vfd.printPacket("%-8s ", buffer);
	}
	
	
	if( last_heading != INT_MAX){
		char buffer[12];
		
		string ordinal[] =  {"N ","NE","E ", "SE","S ","SW","W ","NW"} ;
		string dir = ordinal[int(floor((last_heading / 45) + 0.5)) % 8]  ;
		
		memset(buffer, ' ', sizeof(buffer));
		sprintf( buffer , "%3d\xa0\x1c%2s\x1d ",last_heading, dir.c_str());
		_vfd.setCursor(midX +20 ,utmRow+20);
		_vfd.printPacket("%-8s ", buffer);
	}
	else {
		_vfd.setCursor(midX +20 ,utmRow+20);
		_vfd.printPacket("%-8s ", "---");
	}
	
	drawTimeBox();
}


void DisplayMgr::drawTimeBox(){
	// Draw time
	
	time_t rawtime;
	struct tm timeinfo = {0};
	
	time(&rawtime);
	localtime_r(&rawtime, &timeinfo); // fills in your structure,
	// instead of returning a pointer to a static
	
	char timebuffer[16] = {0};
	std::strftime(timebuffer, sizeof(timebuffer)-1, "%2l:%M%P", &timeinfo);
	_vfd.setFont(VFD::FONT_5x7);
	_vfd.setCursor(_vfd.width() - (strlen(timebuffer) * 6) ,7);
	_vfd.write(timebuffer);
	
}

// MARK: -  Info Screen

void DisplayMgr::drawInfoScreen(modeTransition_t transition){
	
	string str;
 
	PiCarMgr*			mgr 	= PiCarMgr::shared();
	PiCarDB*				db 		= mgr->db();
#if USE_COMPASS
	CompassSensor* 	compass	= mgr->compass();
#endif
	

	static int lastOffset = 0;
	static int firstLine = 0;
	static vector<vector<string>> rows = {};
	bool needsRedraw = false;

	
	if(transition == TRANS_LEAVING) {
		
		_rightKnob.setAntiBounce(antiBounceDefault);
		_vfd.clearScreen();
		lastOffset = 0;
		firstLine = 0;
		return;
	}
	
	 
	if(transition == TRANS_ENTERING){
		_rightKnob.setAntiBounce(antiBounceSlow);
	 
	 		_vfd.clearScreen();
		// top line
		_vfd.setCursor(0, 7);
		_vfd.setFont(VFD::FONT_5x7);
		_vfd.printPacket("PiCar ");
		
		str = string(PiCarMgr::PiCarMgr_Version);
		std::transform(str.begin(), str.end(),str.begin(), ::toupper);
		_vfd.setFont(VFD::FONT_MINI); _vfd.printPacket("%s", str.c_str());

		// safety check
		if(_lineOffset >=  10)
			_lineOffset = 0;
		
		lastOffset = INT_MAX;
		firstLine = 0;
		needsRedraw = true;
		
	}
 
	if(transition == TRANS_LEAVING) {
	 	return;
	}
	
	/* uodate CPU temp*/
	{
		float cTemp = 0;
		int  fanspeed = 0;
		
		if(db->getFloatValue(VAL_CPU_INFO_TEMP, cTemp)){
			
			_vfd.setCursor(10,16  );
			
			_vfd.setFont(VFD::FONT_MINI);
			_vfd.printPacket("CPU TEMP: ");
			
			_vfd.setFont(VFD::FONT_5x7);
			_vfd.printPacket("%d\xa0" "C ", (int) round(cTemp));
			
			if(db->getIntValue(VAL_FAN_SPEED, fanspeed)){
				
				_vfd.setFont(VFD::FONT_MINI);
				_vfd.printPacket("FAN: ");
				_vfd.setFont(VFD::FONT_5x7);
				
				char buffer[10];
				
				if(fanspeed == 0){
					sprintf(buffer, "%-4s", "OFF");
				}
				else
				{
					sprintf(buffer, "%d%%", fanspeed);
				}
				
				_vfd.printPacket("%-4s ", buffer);
			}
		}
		
	}

	if(needsRedraw){
		
		rows = {};
		
		/* Get build Date*/
		rows.push_back( {"DATE: ", string(__DATE__)  + " " +  string(__TIME__)});
		
		/* Get OS version*/
		{
			struct utsname utsBuff;
			uname(&utsBuff);
			rows.push_back( {"LINUX: ", string(utsBuff.sysname)  + ": " +  string(utsBuff.release)});
		}
		/* Get RTL_SDR ID*/
		{
			RadioMgr*			radio 	= mgr->radio();
			RtlSdr::device_info_t rtlInfo;
			if(radio->isConnected() && radio->getDeviceInfo(rtlInfo) ){
				rows.push_back( {"RADIO: ", string(rtlInfo.product)});
			}
		}
		
		/* GPS Status*/
		GPSmgr*				gps 		= mgr->gps();
		rows.push_back( {"GPS: ", gps->isConnected()?"OK":"MOT CONNECTED"} );
		
		/* CAN BUS*/
		{
			PiCarCAN*			can 		= mgr->can();
			vector<CANBusMgr::can_status_t> canStats;
			string str = "";
			
			if(can->getStatus(canStats)){
				for(auto e :canStats){
					str  += " " + e.ifName;
				}
			}
			else
				str = "NOT CONNECTED" ;
			rows.push_back( {"CAN: ", str });
		}
		
		/* WIFI */
		{
			string str = "";
			stringvector wifiPorts;
			mgr->hasWifi(&wifiPorts);
			
			if(wifiPorts.size() == 0){
				str  +=  "OFF";
			}
			else {
				for(auto s :wifiPorts){
					str  += " " + s;
				}
			}
			rows.push_back( {"WIFI: ", str });
		}
		
		constexpr int displayedLines = 6;
		
		constexpr int top = 16+5 + 2;
		
		_vfd.printRows(top, 9 , rows, firstLine, displayedLines, VFD::FONT_MINI);
		
		if(rows.size() > displayedLines){
			
			float bar_height =  (float)(displayedLines +1)/ (float)rows.size() ;
			float offset =  (float)_lineOffset / ((float)rows.size() -1) ;
			
			_vfd.drawScrollBar(top, bar_height ,offset);
		}
	}
		
/*
		_vfd.clearScreen();
		// top line
		_vfd.setCursor(col, row);
		_vfd.setFont(VFD::FONT_5x7);
		_vfd.printPacket("Car Radio ");
		
		struct utsname utsBuff;
		RtlSdr::device_info_t rtlInfo;
		
		str = string(PiCarMgr::PiCarMgr_Version);
		std::transform(str.begin(), str.end(),str.begin(), ::toupper);
		_vfd.setFont(VFD::FONT_MINI); _vfd.printPacket("%s", str.c_str());
		
		row += 7;  _vfd.setCursor(col+10, row );
		str = "DATE: " + string(__DATE__)  + " " +  string(__TIME__);
		std::transform(str.begin(), str.end(),str.begin(), ::toupper);
		_vfd.printPacket("%s", str.c_str());
		
		uname(&utsBuff);
		row += 7;  _vfd.setCursor(col+10, row );
		str =   string(utsBuff.sysname)  + ": " +  string(utsBuff.release);
		std::transform(str.begin(), str.end(),str.begin(), ::toupper);
		_vfd.printPacket("%s", str.c_str());
		
		row += 7;  _vfd.setCursor(col+10, row );
		if(radio->isConnected() && radio->getDeviceInfo(rtlInfo) )
			str =   "RADIO: " +  string(rtlInfo.product);
		else
			str =   string("RADIO: ") + string("NOT CONNECTED");
		std::transform(str.begin(), str.end(),str.begin(), ::toupper);
		_vfd.printPacket("%s", str.c_str());
		
		row += 7;  _vfd.setCursor(col+10, row );
		if(gps->isConnected() && radio->getDeviceInfo(rtlInfo) )
			str =   string("GPS: ") + string("OK");
		else
			str =   string("GPS: ") + string("NOT CONNECTED");
		std::transform(str.begin(), str.end(),str.begin(), ::toupper);
		_vfd.printPacket("%s", str.c_str());
		
#if USE_COMPASS
		row += 7;  _vfd.setCursor(col+10, row );
		string compassVersion;
		if(compass->isConnected() && compass->versionString(compassVersion))
			str =   string("COMPASS: ") + compassVersion;
		else
			str =   string("COMPASS: ") + string("NOT CONNECTED");
		std::transform(str.begin(), str.end(),str.begin(), ::toupper);
		_vfd.printPacket("%s", str.c_str());
#endif
		
		// save this in static
		lastrow = row;
	}
	
	row = lastrow;
	{
		row = row + 7;
		_vfd.setCursor(col+10, row );
		_vfd.setFont(VFD::FONT_MINI);
		
		vector<CANBusMgr::can_status_t> canStats;
		if(can->getStatus(canStats)){
			str =  string("CAN:");
			for(auto e :canStats){
				str  += " " + e.ifName;
			}
		}
		else
			str =  string("CAN: ") + string("NOT CONNECTED");
		
		std::transform(str.begin(), str.end(),str.begin(), ::toupper);
		_vfd.printPacket("%-30s", str.c_str());
	}
	
	{
		row = row + 7;
		_vfd.setCursor(col+10, row );
		_vfd.setFont(VFD::FONT_MINI);
		stringvector wifiPorts;
		mgr->hasWifi(&wifiPorts);
		str =  string("WIFI: ");
		
		if(wifiPorts.size() == 0){
			str  +=  "OFF";
		}
		else {
			for(auto s :wifiPorts){
				str  += " " + s;
			}
		}
		std::transform(str.begin(), str.end(),str.begin(), ::toupper);
		_vfd.printPacket("%-30s", str.c_str());
	}
	


*/
	//	printf("displayStartupScreen %s\n",redraw?"REDRAW":"");
}


bool DisplayMgr::processSelectorKnobActionForInfo( knob_action_t action){
	bool wasHandled = false;
	
	switch(action){
	 
		case KNOB_UP:
			if(_lineOffset < 255){
				_lineOffset++;
				setEvent(EVT_NONE,MODE_INFO);
			}
			wasHandled = true;
			break;
			
		case KNOB_DOWN:
			if(_lineOffset != 0) {
				_lineOffset--;
				setEvent(EVT_NONE,MODE_INFO);
			}
			wasHandled = true;
			break;
			
		case KNOB_EXIT:
		case KNOB_CLICK:
		case KNOB_DOUBLE_CLICK:
			setEvent(EVT_POP, MODE_UNKNOWN);
			_lineOffset = 0;
 			wasHandled = true;
	 			break;
			
		default: break;
			
	}
	
	return wasHandled;
}

// MARK: -  Scanner Screen

void DisplayMgr::drawScannerScreen(modeTransition_t transition){
	
	PiCarMgr* mgr	= PiCarMgr::shared();
	RadioMgr* radio 	= PiCarMgr::shared()->radio();
	
	
	// 	printf("drawScannerScreen(%d)\n", transition);
	//	int centerX = _vfd.width() /2;
	int centerY = _vfd.height() /2;
	
	if(transition == TRANS_ENTERING){
		_vfd.clearScreen();
		
		_vfd.setCursor(0, 60);
		if(mgr->isPresetChannel(RadioMgr::SCANNER, 0)){
			_vfd.setFont(VFD::FONT_MINI);
			_vfd.printPacket("PRESET");
		}
		else {
			_vfd.printPacket("      ");
		}
	}
	
	
	if(transition == TRANS_LEAVING) {
		LEDeventScannerStop();
		return;
	}
	
	if(transition ==  TRANS_REFRESH) {
		// 		LEDeventScannerStep();
	}
	
	if(transition ==  TRANS_IDLE) {
		//   		LEDeventScannerHold();
	}
	
	RadioMgr::radio_mode_t  mode;
	uint32_t						freq;
	
	bool foundSignal = radio->getCurrentScannerChannel(mode, freq);
	
	if(foundSignal){
		_vfd.setFont(VFD::FONT_5x7);
		
		constexpr int maxLen = 21;
		string spaces(maxLen, ' ');
		
		string titleStr = "";
		PiCarMgr::station_info_t info;
		if(mgr->getStationInfo(mode, freq, info)){
			titleStr = truncate(info.title, maxLen);
			string portionOfSpaces = spaces.substr(0, (maxLen - titleStr.size()) / 2);
			titleStr = portionOfSpaces + titleStr;
		}
		
		_vfd.setCursor(0,centerY-5);
		_vfd.printPacket("%-20s",titleStr.c_str() );
		
		string channelStr = RadioMgr::modeString(mode) + " "
		+ RadioMgr::hertz_to_string(freq, 3) + " "
		+ RadioMgr::freqSuffixString(freq);
		
		string portionOfSpaces = spaces.substr(0, (maxLen - channelStr.size()) / 2);
		channelStr = portionOfSpaces + channelStr;
		_vfd.setCursor(0,centerY+5);
		_vfd.printPacket("%-20s",channelStr.c_str() );
		
		drawReceptionBars(0, centerY+19, radio->get_if_level());
	}
	
	
	drawEngineCheck();
	drawTemperature();
	drawTimeBox();
}
// MARK: -  Dimmer Screen


void DisplayMgr::drawDimmerScreen(modeTransition_t transition){
	
	PiCarMgr* mgr	= PiCarMgr::shared();
	
	uint8_t width = _vfd.width();
	uint8_t height = _vfd.height();
	uint8_t midX = width/2;
	uint8_t midY = height/2;
	
	uint8_t leftbox 	= 20;
	uint8_t rightbox 	= width - 20;
	uint8_t topbox 	= midY -5 ;
	uint8_t bottombox = midY + 5 ;
	
	if(transition == TRANS_LEAVING) {
		return;
	}
	
	if(transition == TRANS_ENTERING) {
		_vfd.clearScreen();
		
		// draw centered heading
		_vfd.setFont(VFD::FONT_5x7);
		string str = "Dim Screen";
		_vfd.setCursor( midX - ((str.size()*5) /2 ), topbox - 5);
		_vfd.write(str);
		
		//draw box outline
		uint8_t buff1[] = {VFD::VFD_OUTLINE,leftbox,topbox,rightbox,bottombox };
		_vfd.writePacket(buff1, sizeof(buff1), 0);
	}
	
	// brightness scales between 0 - 1.0
	float dim =  mgr->dimLevel();
	uint8_t itemX = leftbox +  (rightbox - leftbox) * dim;
	
	// clear rest of inside of box
	if(dim < 1)
	{
		// there is some kind of bug in the Noritake VFD where id you send
		// VFD_CLEAR_AREA  followed by a 0x60, it screws up the display
		uint8_t start = itemX+1;
		if(start == 96) start = 95;
		
		uint8_t buff2[] = {
			VFD::VFD_CLEAR_AREA,
			// static_cast<uint8_t>(itemX+1),  static_cast<uint8_t> (topbox+1),
			static_cast<uint8_t>(start),  static_cast<uint8_t> (topbox+1),
			static_cast<uint8_t> (rightbox-1),static_cast<uint8_t> (bottombox-1)};
		
		_vfd.writePacket(buff2, sizeof(buff2), 1000);
	}
	
	// fill  area box
	uint8_t buff3[] = {VFD::VFD_SET_AREA,
		static_cast<uint8_t>(leftbox), static_cast<uint8_t> (topbox+1),
		static_cast<uint8_t>(itemX),static_cast<uint8_t>(bottombox-1) };
	_vfd.writePacket(buff3, sizeof(buff3), 1000);
	
}

bool DisplayMgr::processSelectorKnobActionForDimmer( knob_action_t action){
	bool wasHandled = false;
	
	PiCarMgr* mgr	= PiCarMgr::shared();
	
	double brightness = mgr->dimLevel();
	double increment = .125;
	
	if(brightness > -1){
		
		if(action == KNOB_UP){
			
			if(brightness < 1.0){
				brightness += increment;
				brightness = min(brightness, 1.0);
				
				mgr->setDimLevel(brightness);
				setBrightness(brightness);
				setEvent(EVT_NONE,MODE_DIMMER);
			}
			wasHandled = true;
		}
		else if(action == KNOB_DOWN){
			
			if(brightness > 0){
				brightness -= increment;
				brightness = max(brightness, 0.0);
				
				mgr->setDimLevel(brightness);
				setBrightness(brightness);
				setEvent(EVT_NONE,MODE_DIMMER);
			}
			wasHandled = true;
		}
	}
	
	if(action == KNOB_CLICK){
		popMode();
	}
	
	return wasHandled;
}


// MARK: - slidrSelect Screen
void DisplayMgr::showSelectionSilderScreen(
							 string title,
							 std::vector<string> choices,
							 int initialChoice,
							 time_t timeout,
							 menuSelectionSilderSetterCallBack_t setterCB ,
														 boolCallback_t doneCB){
	if(_menuSelectionSliderCBInfo) free(_menuSelectionSliderCBInfo);
	menuSelectionSliderCBInfo_t * cbInfo = (menuSelectionSliderCBInfo_t *) malloc(sizeof( menuSelectionSliderCBInfo_t));
	memset(cbInfo, 0, sizeof( menuSelectionSliderCBInfo_t));
	
	cbInfo->title = title;
	cbInfo->choices = choices;
	cbInfo->currentChoice = initialChoice;
 	cbInfo->timeout = timeout?timeout:5;  // default 5 secs
 	cbInfo->setCB = setterCB;
	cbInfo->doneCB = doneCB;
	
	_menuSelectionSliderCBInfo = cbInfo;
	
	setEvent(EVT_PUSH, MODE_SELECT_SLIDER );

}

bool DisplayMgr::processSelectorKnobActionForSelectSlider( knob_action_t action){
	bool wasHandled = false;
 
	auto info = _menuSelectionSliderCBInfo;
	// guard
	if(!info) return false;
 
	if(action == KNOB_UP){
		
		if(info->currentChoice < info->choices.size() -1) {
			info->currentChoice++;
			if(info->setCB)(info->setCB)(info->currentChoice);
			setEvent(EVT_NONE,MODE_SELECT_SLIDER);
		}
			wasHandled = true;
	}
	
	else if(action == KNOB_DOWN){
		
		if(info->currentChoice > 0) {
			info->currentChoice--;
			if(info->setCB)(info->setCB)(info->currentChoice);
			setEvent(EVT_NONE,MODE_SELECT_SLIDER);
		}
		wasHandled = true;
	}
	else if(action == KNOB_CLICK){
		
		auto savedCB = _menuSelectionSliderCBInfo->doneCB;
		
		free(_menuSelectionSliderCBInfo);
		_menuSelectionSliderCBInfo = NULL;
		
		popMode();
	 
		if(savedCB){
			(savedCB)(true);
			wasHandled = true;
		}
	}
	
	return wasHandled;
}
 

void DisplayMgr::drawSelectSliderScreen(modeTransition_t transition){
	
	//	printf("drawSelectSliderScreen(%d)\n", transition);
	
	uint8_t width = _vfd.width();
	uint8_t height = _vfd.height();
	uint8_t midX = width/2;
	uint8_t midY = height/2;
	
	uint8_t leftbox 	= 20;
	uint8_t rightbox 	= width - 20;
	uint8_t topbox 	= midY -5 ;
	uint8_t bottombox = midY + 5 ;
	
	if(transition == TRANS_LEAVING) {
		
		if(_menuSelectionSliderCBInfo) free(_menuSelectionSliderCBInfo);
		_menuSelectionSliderCBInfo  = NULL;
		return;
	}
	
	if(!_menuSelectionSliderCBInfo)
		return;
	
	if(transition == TRANS_ENTERING) {
		_vfd.clearScreen();
		
		// draw centered heading
		_vfd.setFont(VFD::FONT_5x7);
		string str = _menuSelectionSliderCBInfo->title;
		_vfd.setCursor( midX - ((str.size()*5) /2 ), topbox - 5);
		_vfd.write(str);
		
		//draw box outline
		uint8_t buff1[] = {VFD::VFD_OUTLINE,leftbox,topbox,rightbox,bottombox };
		_vfd.writePacket(buff1, sizeof(buff1), 0);
	}
	
	if(transition == TRANS_ENTERING || transition == TRANS_REFRESH){
		
		auto boxwidth = (rightbox - leftbox);
		int count = (int) _menuSelectionSliderCBInfo->choices.size();
		auto step =  static_cast<float>(boxwidth) / static_cast<float>(count) ;
		uint8_t itemX = (step * _menuSelectionSliderCBInfo->currentChoice) + leftbox;
		//		printf("step: %f count: %d boxwidth: %d \n", step, count, boxwidth);
		//				printf("itemX: %2d\t choices: %3d\t choice: %2d\n", itemX,
		//						 count, _menuSelectionSliderCBInfo->currentChoice  );
		
		itemX &= 0xfE; // to nearest 2
		itemX = max(itemX,  static_cast<uint8_t> (leftbox+2) );
		itemX = min(itemX,  static_cast<uint8_t> (rightbox-6) );
		
		// there is some kind of bug in the Noritake VFD where id you send
		// VFD_CLEAR_AREA  followed by a 0x60, it screws up the display
		// To send commands as hexadecimal, prefix the 2 bytes using character 60H.
		// To send character 60H to the display, send 60H twice.
		
		if(itemX == 96) itemX = 95;
		
		_vfd.setFont(VFD::FONT_5x7);
		
		// clear inside of box
		uint8_t buff2[] = {VFD::VFD_CLEAR_AREA,
			static_cast<uint8_t>(leftbox+1), static_cast<uint8_t> (topbox+1),
			static_cast<uint8_t>(rightbox-1),static_cast<uint8_t>(bottombox-1),
			//		VFD_SET_CURSOR, midX, static_cast<uint8_t>(bottombox -1),'|',
			// draw marker
			VFD::VFD_SET_WRITEMODE, 0x03, 	// XOR
			VFD::VFD_SET_CURSOR, itemX, static_cast<uint8_t>(bottombox -1), 0xBB,
			VFD::VFD_SET_WRITEMODE, 0x00,};	// Normal
		
		_vfd.writePacket(buff2, sizeof(buff2), 0);
		
		constexpr int maxLen = 20;
		string spaces(maxLen, ' ');
		
		string valStr =  truncate(_menuSelectionSliderCBInfo->choices[_menuSelectionSliderCBInfo->currentChoice],maxLen);
		string portionOfSpaces = spaces.substr(0, (maxLen - valStr.size()) / 2);
		valStr = portionOfSpaces + valStr + portionOfSpaces;
		
		_vfd.setFont(VFD::FONT_5x7);
		_vfd.setCursor( 0, bottombox + 10);
		_vfd.write(valStr);
	}
}
 

// MARK: -  Slider Screen
void DisplayMgr::showSliderScreen(
											 string title,
											 string right_text,
											 string left_text,
											 time_t timeout,
											 menuSliderGetterCallBack_t getterCB ,
											 menuSliderSetterCallBack_t setterCB,
											 boolCallback_t doneCB){
	
	if(_menuSliderCBInfo) free(_menuSliderCBInfo);
	menuSliderCBInfo_t * cbInfo = (menuSliderCBInfo_t *) malloc(sizeof( menuSliderCBInfo_t));
	memset(cbInfo, 0, sizeof( menuSliderCBInfo_t));
	
	cbInfo->title = title;
	cbInfo->right_text = right_text;
	cbInfo->left_text = left_text;
	cbInfo->timeout = timeout?timeout:5;  // default 5 secs
	cbInfo->getCB = getterCB;
	cbInfo->setCB = setterCB;
	cbInfo->doneCB = doneCB;
	
	_menuSliderCBInfo = cbInfo;
	
	setEvent(EVT_PUSH, MODE_SLIDER );
}


void DisplayMgr::drawSliderScreen(modeTransition_t transition){
	
//	printf("drawSliderScreen(%d)\n", transition);
	
	uint8_t width = _vfd.width();
	uint8_t height = _vfd.height();
	uint8_t midX = width/2;
	uint8_t midY = height/2;
	
	uint8_t leftbox 	= 20;
	uint8_t rightbox 	= width - 20;
	uint8_t topbox 	= midY -5 ;
	uint8_t bottombox = midY + 5 ;
	
	if(transition == TRANS_LEAVING) {
		
		if(_menuSliderCBInfo) free(_menuSliderCBInfo);
		_menuSliderCBInfo  = NULL;
		return;
	}
	
	if(!_menuSliderCBInfo)
		return;
	
	if(transition == TRANS_ENTERING) {
		_vfd.clearScreen();
		
		// draw centered heading
		_vfd.setFont(VFD::FONT_5x7);
		string str = _menuSliderCBInfo->title;
		_vfd.setCursor( midX - ((str.size()*5) /2 ), topbox - 5);
		_vfd.write(str);
		
		//draw box outline
		uint8_t buff1[] = {VFD::VFD_OUTLINE,leftbox,topbox,rightbox,bottombox };
		_vfd.writePacket(buff1, sizeof(buff1), 0);
		
		_vfd.setCursor(leftbox - 10, bottombox -1 );
		_vfd.write(_menuSliderCBInfo->left_text);
		_vfd.setCursor(rightbox + 5, bottombox -1 );
		_vfd.write(_menuSliderCBInfo->right_text);
	}
	
	
	// avoid doing a needless refresh.  if this was a timeout event,  then just update the time
	if(transition == TRANS_ENTERING || transition == TRANS_REFRESH){
		double val =  0;
	 
		if(_menuSliderCBInfo->getCB)
			val = _menuSliderCBInfo->getCB();
 
		uint8_t itemX = midX +  ((rightbox - leftbox)/2) * val;
		itemX-=1;		// correct for fencepost issues.
		midX-=2;
		itemX &= 0xfE; // to nearest 2
		itemX = max(itemX,  static_cast<uint8_t> (leftbox+2) );
		itemX = min(itemX,  static_cast<uint8_t> (rightbox-5) );

		// clear inside of box
		uint8_t buff2[] = {VFD::VFD_CLEAR_AREA,
			static_cast<uint8_t>(leftbox+1), static_cast<uint8_t> (topbox+1),
			static_cast<uint8_t>(rightbox-1),static_cast<uint8_t>(bottombox-1),
			VFD::VFD_SET_CURSOR, midX, static_cast<uint8_t>(bottombox -1),'|',
			// draw marker
			VFD::VFD_SET_WRITEMODE, 0x03, 	// XOR
			VFD::VFD_SET_CURSOR, itemX, static_cast<uint8_t>(bottombox -1), 0x5F,
			VFD::VFD_SET_WRITEMODE, 0x00,};	// Normal
		
		_vfd.writePacket(buff2, sizeof(buff2), 0);
		
	}
}
 
bool DisplayMgr::processSelectorKnobActionForSlider( knob_action_t action){
	bool wasHandled = false;
	
	double val =  0;
	
	// guard
	if(!_menuSliderCBInfo) return false;
	
	if(_menuSliderCBInfo->getCB)
		val = _menuSliderCBInfo->getCB();
	
	// limit the precision
	val = std::floor((val * 100) + .5) / 100;
	
	if(action == KNOB_UP){
		
		if(_menuSliderCBInfo->setCB){
			if(val < .9){
				(_menuSliderCBInfo->setCB)(val +.1);
				setEvent(EVT_NONE,MODE_SLIDER);
			}
		}
		wasHandled = true;
	}
	
	else if(action == KNOB_DOWN){
		
		if(_menuSliderCBInfo->setCB){
			if(val > -.9){
				(_menuSliderCBInfo->setCB)(val -.1);
				setEvent(EVT_NONE,MODE_SLIDER);
			}
		}
		
		wasHandled = true;
	}
	else if(action == KNOB_CLICK){
		
		auto savedCB = _menuSliderCBInfo->doneCB;
		
		free(_menuSliderCBInfo);
		_menuSliderCBInfo = NULL;
		
		popMode();
	 
		if(savedCB){
			(savedCB)(true);
			wasHandled = true;
		}
 	}
	
	return wasHandled;
}
 

// MARK: -  Squelch  Screen


void DisplayMgr::drawSquelchScreen(modeTransition_t transition){
	
	RadioMgr*	radio 	= PiCarMgr::shared()->radio();
	int			 maxSquelch = radio->getMaxSquelchRange();
	
	uint8_t width = _vfd.width();
	uint8_t height = _vfd.height();
	uint8_t midX = width/2;
	uint8_t midY = height/2;
	
	uint8_t leftbox 	= 20;
	uint8_t rightbox 	= width - 20;
	uint8_t topbox 	= midY -5 ;
	uint8_t bottombox = midY + 5 ;
	
	//	printf("drawSquelchScreen(%d)\n", transition);
	
	if(transition == TRANS_LEAVING) {
		return;
	}
	
	if(transition == TRANS_ENTERING) {
		_vfd.clearScreen();
		
		//draw box outline
		uint8_t buff1[] = {VFD::VFD_OUTLINE,leftbox,topbox,rightbox,bottombox };
		_vfd.writePacket(buff1, sizeof(buff1), 0);
	}
	
	// avoid doing a needless refresh.  if this was a timeout event,  then just update the time
	if(transition == TRANS_ENTERING || transition == TRANS_REFRESH){
		
		int squelch = radio->getSquelchLevel();
		
		auto boxwidth = (rightbox - leftbox);
		auto step =  static_cast<float>(boxwidth) / static_cast<float>(abs(maxSquelch)) ;
		uint8_t itemX = (step * squelch) + rightbox;
		
	//	printf("itemX: %2d\t maxSquelch: %3d\t squelch: %2d\n", itemX, abs(maxSquelch), squelch  );
		itemX &= 0xfE; // to nearest 2
		itemX = max(itemX,  static_cast<uint8_t> (leftbox+2) );
		itemX = min(itemX,  static_cast<uint8_t> (rightbox-6) );
		
		_vfd.setFont(VFD::FONT_5x7);
		
		// clear inside of box
		uint8_t buff2[] = {VFD::VFD_CLEAR_AREA,
			static_cast<uint8_t>(leftbox+1), static_cast<uint8_t> (topbox+1),
			static_cast<uint8_t>(rightbox-1),static_cast<uint8_t>(bottombox-1),
			//		VFD_SET_CURSOR, midX, static_cast<uint8_t>(bottombox -1),'|',
			// draw marker
			VFD::VFD_SET_WRITEMODE, 0x03, 	// XOR
			VFD::VFD_SET_CURSOR, itemX, static_cast<uint8_t>(bottombox -1), 0xBB,
			VFD::VFD_SET_WRITEMODE, 0x00,};	// Normal
		
		_vfd.writePacket(buff2, sizeof(buff2), 0);
		
		// draw centered heading
		
		char buffer[64] = {0};
		sprintf(buffer, "Squelch: %-3d",squelch);
		_vfd.setCursor( midX - ((strlen(buffer)*5) /2 ), topbox - 5);
		_vfd.printPacket(buffer);
	}
}

bool DisplayMgr::processSelectorKnobActionForSquelch( knob_action_t action){
	bool wasHandled = false;
	
	RadioMgr*	radio 	= PiCarMgr::shared()->radio();
	
	int maxSquelch = radio->getMaxSquelchRange();
	int squelch = radio->getSquelchLevel();
	
	if(action == KNOB_UP){
		if(squelch < 0){
			radio->setSquelchLevel(squelch +1 );
			setEvent(EVT_NONE,MODE_SQUELCH);
		}
		wasHandled = true;
	}
	
	else if(action == KNOB_DOWN){
		if(squelch > maxSquelch){
			radio->setSquelchLevel(squelch - 1 );
			setEvent(EVT_NONE,MODE_SQUELCH);
		}
		wasHandled = true;
	}
	else if(action == KNOB_CLICK){
		
		setEvent(EVT_POP, MODE_UNKNOWN);
		wasHandled = true;
		
	}
	
	return wasHandled;
}




// MARK: -  DTC codes screen


void DisplayMgr::drawDTCScreen(modeTransition_t transition){
	
	PiCarCAN*	can 	= PiCarMgr::shared()->can();
	FrameDB*		frameDB 	= can->frameDB();
	
	uint8_t width = _vfd.width();
	uint8_t height = _vfd.height();
	
	static uint32_t lastHash = 0;
	static uint8_t lastOffset = 0;
	
	bool needsRedraw = false;
	
	if(transition == TRANS_LEAVING) {
		_lineOffset = 0;
		return;
	}
	
	if(transition == TRANS_ENTERING){
		lastHash = 0;
		_lineOffset = 0;
		
		_vfd.clearScreen();
		_vfd.setFont(VFD::FONT_5x7) ;
		_vfd.setCursor(0,10);
		_vfd.write("DTC Codes");
	}
	
	string stored = "";
	string pending = "";
	frameDB->valueWithKey("OBD_DTC_STORED", &stored);
	frameDB->valueWithKey("OBD_DTC_PENDING", &pending);
	uint32_t hash = XXHash32::hash(stored+pending);
	
	stringvector vCodes = split<string>(stored, " ");
	auto totalStored = vCodes.size();
	
	stringvector vPending = split<string>(pending, " ");
	auto totalPending = vPending.size();
	auto totalCodes = totalStored + totalPending;
	vCodes.insert(vCodes.end(), vPending.begin(), vPending.end());
	
	// if anything changed, redraw
	
	if(hash != lastHash){
		lastHash = hash;
		_lineOffset = 0;
		
		uint8_t buff2[] = {VFD::VFD_CLEAR_AREA,
			static_cast<uint8_t>(0),  static_cast<uint8_t> (10),
			static_cast<uint8_t> (width),static_cast<uint8_t> (height)};
		_vfd.writePacket(buff2, sizeof(buff2), 1000);
		
		needsRedraw = true;
	}
	
	
	if( lastOffset != _lineOffset){
		if(totalCodes > 0){
			lastOffset = _lineOffset;
			needsRedraw = true;
		}
	}
	if(needsRedraw){
		needsRedraw = false;
		
		if(totalCodes == 0 ){
			_vfd.setCursor(10,height/2);
			_vfd.write("No Codes");
			
		}
		else {
			
			// draw codes with selected code boxed
			vector<string> lines = {};
			size_t displayedLines = 6;
			size_t firstLine = 0;
			int codesPerLine = 4;
			
			if(totalPending)
			{
				lines.push_back("PENDING: " + to_string(totalPending));
				
				string line = " ";
				int cnt = 0;
				for(auto i = 0 ; i < totalPending; i++){
					bool isSelected = i == _lineOffset;
					if(isSelected) firstLine = lines.size() -1;
					line+=  (isSelected?"[":" ") + vCodes[i] + (isSelected?"] ":"  ");
					if(++cnt < codesPerLine) continue;
					lines.push_back(line);
					line = " ";
					cnt = 0;
				}
				if(cnt > 0){
					lines.push_back(line);
				}
				lines.push_back("");
			}
			
			if(totalStored)
			{
				lines.push_back("STORED: " + to_string(totalStored));
				
				string line = " ";
				int cnt = 0;
				for(auto i = totalPending ; i < totalCodes; i++){
					bool isSelected = i == _lineOffset;
					if(isSelected) firstLine = lines.size()-1;
					
					line+=  (isSelected?"[":" ") + vCodes[i] + (isSelected?"] ":"  ");
					if(++cnt < codesPerLine) continue;
					lines.push_back(line);
					line = " ";
					cnt = 0;
				}
				if(cnt > 0){
					lines.push_back(line);
				}
				lines.push_back("");
			}
			
			if(_lineOffset < totalCodes){
				lines.push_back("  ERASE ALL CODES?  ");
				lines.push_back("  EXIT  ");
				
			}
			else if(_lineOffset == totalCodes){
				lines.push_back("[ ERASE ALL CODES? ]");
				lines.push_back("  EXIT  ");
				firstLine = lines.size()-1;
			}
			else {
				lines.push_back("  ERASE ALL CODES? ");
				lines.push_back(" [EXIT]  ");
				firstLine = lines.size()-1;
				_lineOffset = totalCodes + 1;
			}
			
			_vfd.setFont(VFD::FONT_MINI) ;
			
			int  maxFirstLine  = (int) (lines.size() - displayedLines);
			if(firstLine > maxFirstLine) firstLine = maxFirstLine;
			
			_vfd.printLines(20, 6, lines, firstLine, displayedLines, VFD::FONT_MINI);
			if(lines.size() > displayedLines){
				
				float bar_height =  (float)(displayedLines +1)/ (float)lines.size() ;
				float offset =  (float)_lineOffset / ((float)lines.size() -1) ;
				
				_vfd.drawScrollBar(11, bar_height ,offset);
			}
		}
	}
	drawTimeBox();
}


bool DisplayMgr::processSelectorKnobActionForDTC( knob_action_t action){
	bool wasHandled = false;
	
	if(action == KNOB_UP){
		if(_lineOffset < 255){
			_lineOffset++;
			setEvent(EVT_NONE,MODE_DTC);
		}
		wasHandled = true;
	}
	else if(action == KNOB_DOWN){
		if(_lineOffset != 0) {
			_lineOffset--;
			setEvent(EVT_NONE,MODE_DTC);
		}
		wasHandled = true;
	}
	else if(action == KNOB_CLICK){
		
		// sigh this code has to calculate the offset the same as drawDTCScreen
		PiCarCAN*	can 	= PiCarMgr::shared()->can();
		FrameDB*		frameDB 	= can->frameDB();
		
		string stored = "";
		string pending = "";
		frameDB->valueWithKey("OBD_DTC_STORED", &stored);
		frameDB->valueWithKey("OBD_DTC_PENDING", &pending);
		stringvector vCodes = split<string>(stored, " ");
		auto totalStored = vCodes.size();
		stringvector vPending = split<string>(pending, " ");
		auto totalPending = vPending.size();
		auto totalCodes = totalStored + totalPending;
		vCodes.insert(vCodes.end(), vPending.begin(), vPending.end());
		
		if(!totalCodes ){
			popMode();
		}
		else if(_lineOffset < totalCodes){
			// select a code
			showDTCInfo(vCodes[_lineOffset].c_str());
			//		printf("code %s\n", vCodes[_lineOffset].c_str());
			wasHandled = true;
		}
		else if(_lineOffset == totalCodes){
			//		erase  from DB OBD_DTC_STORED/
			frameDB->clearValue("OBD_DTC_STORED");
			
			// tell ECU to erase it
			can->sendDTCEraseRequest();
			
			// redraw
			setEvent(EVT_NONE,MODE_DTC);
			
			wasHandled = true;
		}
		else {
			popMode();
		}
		
	}
	
	return wasHandled;
	
}


void DisplayMgr::drawDTCInfoScreen(modeTransition_t transition, string code){
	
	
	if(transition == TRANS_ENTERING){
		
		PiCarCAN*	can 	= PiCarMgr::shared()->can();
		
		_vfd.clearScreen();
		_vfd.setFont(VFD::FONT_MINI) ;
		_vfd.setCursor(0,10);
		_vfd.write("DIAGNOSTIC CODE" );
		
		_vfd.setCursor(0,22);
		_vfd.setFont(VFD::FONT_5x7) ;
		_vfd.printPacket("%s", code.c_str());
		
		string description = "No Description Available";
		
		can->descriptionForDTCCode(code, description);
		std::transform(description.begin(), description.end(),description.begin(), ::toupper);
		
		stringvector lines = Utils::split(description, 30);
		
		_vfd.setFont(VFD::FONT_MINI) ;
		_vfd.printLines(32, 7, lines, 1, 4);
	}
	
	drawTimeBox();
	
}

bool DisplayMgr::processSelectorKnobActionForDTCInfo( knob_action_t action){
	bool wasHandled = false;
	
	if(action == KNOB_UP){
		setEvent(EVT_NONE,MODE_DTC_INFO);
		wasHandled = true;
	}
	else if(action == KNOB_DOWN){
		setEvent(EVT_NONE,MODE_DTC_INFO);
		wasHandled = true;
	}
	else if(action == KNOB_CLICK){
		setEvent(EVT_POP, MODE_UNKNOWN);
		wasHandled = true;
	}
	
	return wasHandled;
	
}

// MARK: -  Edit String


constexpr char DELETE_CHAR  = '\x9F';
constexpr char CLEAR_CHAR  = '\xBD';

static  const char* charChoices =  "\x9F" "\xBD" " " "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz[\\]!\"#$%&'()*+,-./:;<=>?@{|}";

bool DisplayMgr::processSelectorKnobActionForEditString( knob_action_t action){
	bool wasHandled = false;
	
	
	
	switch(action){
		case KNOB_UP:
			
			if(_isEditing)
				_editChoice +=1;
			else
				_currentMenuItem += 1;
			
			setEvent(EVT_NONE,MODE_EDIT_STRING);
			wasHandled = true;
			break;
			
		case KNOB_DOWN:
			if(_isEditing)
				_editChoice = max( _editChoice - 1,  static_cast<int>(0));
			else
				_currentMenuItem = max( _currentMenuItem - 1,  static_cast<int>(0));
			
			setEvent(EVT_NONE,MODE_EDIT_STRING);
			wasHandled = true;
			break;
			
		case KNOB_CLICK:
		{
			if(_currentMenuItem >= _editString.size()){
				auto savedCB = _editCB;
				
				popMode();
				_editCB = NULL;
				
				bool shouldSave =  _currentMenuItem == _editString.size() +1;
				if(savedCB) {
					savedCB(shouldSave, Utils::trim(_editString));
				}
				wasHandled = true;
				
			}
			// did we click on a string?
			
			else if(_currentMenuItem < _editString.size()){
				
				// entering edit mode?
				if(!_isEditing) {
					_isEditing  = true;
					if (_editString.back() != ' ') _editString += ' ';
				}
				else {
					// already in edit mode
					// did we enter a delete  char
					if( charChoices[_editChoice] == DELETE_CHAR){
						_editString.erase(_currentMenuItem,1);
						_currentMenuItem = max( _currentMenuItem - 1,  static_cast<int>(0));
						
						// set the edit choice to the new selected char
						for(int i = 0; i < std::strlen(charChoices); i ++){
							if(_editString[_currentMenuItem] == charChoices[i]){
								_editChoice = i;
								break;
							}
						}
					}
					// did we enter a clear string char
					else  if( charChoices[_editChoice] == CLEAR_CHAR){
						_editString = " ";
						_currentMenuItem = 0;
						
					}
					// are we at the end of a string
					else  if(_currentMenuItem == _editString.size() -1){
						// did we enter a  space?
						if( charChoices[_editChoice] == ' '){
							// this means we want to leave edit mode
							_isEditing = false;
							
						}
						else {
							// stay in edit mode and move forward
							if (_editString.back() != ' ') {
								_editString += ' ';
								_editChoice = 2; //  location of space
							}
							_currentMenuItem++;
						}
					}
					// in a string and entring a normal char
					else {
						// break edit mode and mode
						_currentMenuItem++;
						//						_currentMenuItem = min(_currentMenuItem ,  static_cast<int>( _editString.size()));
						_isEditing = false;
					}
				}
				
				setEvent(EVT_NONE,MODE_EDIT_STRING);
				wasHandled = true;
				
			}
		}
		default: break;
	}
	
	//	printf("Knob %d  %2d - %s\n", action, _currentMenuItem, _isEditing?"Edit":"no Edit");
	
	return wasHandled;
}


void DisplayMgr::drawEditStringScreen(modeTransition_t transition){
	
	uint8_t height = _vfd.height();
	//	uint8_t width = _vfd.width();
	// 	int centerX = width /2;
	int centerY = _vfd.height() /2;
	
	if(transition == TRANS_ENTERING) {
		_vfd.clearScreen();
		
		_vfd.setFont(VFD::FONT_5x7);
		_vfd.setCursor(0,7);
		_vfd.printPacket("%-14s", _menuTitle.c_str());
		
		_editString += " ";
		_currentMenuItem = 0;
		_menuCursor = 0;
		_isEditing = false;
	}
	
	if(transition == TRANS_LEAVING) {
		return;
	}
	
	
	static int lastItem = INT_MAX;
	static bool lasEditMode = false;
	static int lastEditChoice = INT_MAX;
	
	_currentMenuItem = min(_currentMenuItem ,  static_cast<int>( _editString.size() + 3));
	_editChoice  = min(_editChoice ,  static_cast<int>( strlen(charChoices)));
	
	if(lastItem  != _currentMenuItem || lasEditMode != _isEditing || _editChoice != lastEditChoice){
		
		int startCursor = 30;
		int strlen = (int) _editString.size();
		
		if(lasEditMode == false && _isEditing){
			if(_currentMenuItem < strlen  )
				for(int i = 0; i < std::strlen(charChoices); i ++){
					if(_editString[_currentMenuItem] == charChoices[i]){
						_editChoice = i;
						break;
					}
				}
		}
		lastEditChoice = _editChoice;
		lastItem = _currentMenuItem;
		lasEditMode = _isEditing;
		
		_vfd.setCursor( startCursor /*centerX - ((_editString.size()*7) /2 )*/, centerY);
		_vfd.setFont(VFD::FONT_5x7);
		
		if(_isEditing && _currentMenuItem < strlen)
			_editString[_currentMenuItem] = charChoices[_editChoice];
		
		_vfd.printPacket("%-18s", _editString.c_str());
		
		{ // draw cursor
			_vfd.setCursor( startCursor /*centerX - ((_editString.size()*7) /2 )*/, centerY+8);
			char buf1[20] = {0};
			for(int i = 0; i < strlen; i++){
				buf1[i] = (i == _currentMenuItem)? (_isEditing?'\xa0':'\xaf') :' ';
			}
			_vfd.printPacket("%-18s", buf1);
		}
		
		//// debug
		//		_vfd.setCursor(0, centerY + 10);
		//		_vfd.printPacket("%2d", _currentMenuItem);
		//	//
		//		_vfd.setCursor(0, centerY + 10);
		//		_vfd.printPacket("\x1A\x40\x18\x08\x1C\x5C\x48\x3E\x1D\x1D\x14\x36");
		
		
		_vfd.setCursor(0,height-10);
		_vfd.printPacket("%s Cancel", _currentMenuItem == strlen? "\xb9":" ");
		
		_vfd.setCursor(0,height);
		_vfd.printPacket("%s Save", _currentMenuItem == strlen+1? "\xb9":" ");
	}
	
	drawTimeBox();
}


// MARK: -  GPS waypoints


bool DisplayMgr::processSelectorKnobActionForGPS( knob_action_t action){
	bool wasHandled = false;
	
	auto savedCB = _knobCB;
	
	if(action == KNOB_DOUBLE_CLICK){
		
		popMode();
		_knobCB = NULL;
		wasHandled = true;
		
		if(savedCB) {
			savedCB(action);
		}
	}
	
	return wasHandled;
}

void DisplayMgr::showWaypoints(string intitialUUID,
										 time_t timeout,
										 showWaypointsCallBack_t cb ){
	
	_lineOffset = 0;
	
	// set _lineOffset to proper entry
	if(! intitialUUID.empty()){
		PiCarMgr*		mgr 	= PiCarMgr::shared();
		auto wps 	= mgr->getWaypoints();
		
		for( int i = 0; i< wps.size(); i++){
			if(wps[i].uuid == intitialUUID){
				_lineOffset = i;
				break;
			}
		}
	}
	_wayPointCB = cb;
	_menuTimeout = timeout;
	
	setEvent(EVT_PUSH, MODE_GPS_WAYPOINTS);
}

void DisplayMgr::showWaypoint(string uuid, showWaypointsCallBack_t cb ){
	
	if(! uuid.empty()){
		PiCarMgr*		mgr 	= PiCarMgr::shared();
		auto wps 	= mgr->getWaypoints();
		
		for( int i = 0; i< wps.size(); i++){
			if(wps[i].uuid == uuid){
				_lineOffset = i;
				break;
			}
		}
		_wayPointCB = cb;
		setEvent(EVT_PUSH, MODE_GPS_WAYPOINT);
	}
}

//typedef std::function<void(bool didSucceed, string uuid, knob_action_t action)> showWaypointsCallBack_t;

bool DisplayMgr::processSelectorKnobActionForGPSWaypoints( knob_action_t action){
	bool wasHandled = false;
	
	switch(action){
			
		case KNOB_EXIT:
			if(_wayPointCB) {
				_wayPointCB(false, "", action);
			}
			setEvent(EVT_POP, MODE_UNKNOWN);
			_wayPointCB = NULL;
			_lineOffset = 0;
			break;
			
		case KNOB_UP:
			if(_lineOffset < 255){
				_lineOffset++;
				setEvent(EVT_NONE,MODE_GPS_WAYPOINTS);
			}
			wasHandled = true;
			break;
			
		case KNOB_DOWN:
			if(_lineOffset != 0) {
				_lineOffset--;
				setEvent(EVT_NONE,MODE_GPS_WAYPOINTS);
			}
			wasHandled = true;
			break;
			
		case KNOB_CLICK:
		case KNOB_DOUBLE_CLICK:
		{
			PiCarMgr*	mgr 	= PiCarMgr::shared();
			auto wps 	= mgr->getWaypoints();
			bool success = false;
			
			auto savedCB = _wayPointCB;
			string uuid = "";
			
			if(_lineOffset < wps.size()) {
				uuid = wps[_lineOffset].uuid;
				success = true;
			};
			
			popMode();
			_wayPointCB = NULL;
			_lineOffset = 0;
			
			if(savedCB) {
				savedCB(success, uuid, action);
			}
			
			if(!success)
				setEvent(EVT_REDRAW, _current_mode);
			
			wasHandled = true;
		}
			break;
			
		default: break;
			
	}
	
	return wasHandled;
}


void DisplayMgr::drawGPSWaypointsScreen(modeTransition_t transition){
	
	PiCarMgr*		mgr 	= PiCarMgr::shared();
	GPSmgr*			gps 	= mgr->gps();
	constexpr int displayedLines = 5;
	//
	//	uint8_t width = _vfd.width();
	//	uint8_t height = _vfd.height();
	//
	static int lastOffset = 0;
	static int firstLine = 0;
	
	bool needsRedraw = false;
	
	if(transition == TRANS_LEAVING) {
		
		_rightKnob.setAntiBounce(antiBounceDefault);
		_vfd.clearScreen();
		lastOffset = 0;
		firstLine = 0;
		return;
	}
	
	auto wps 	= mgr->getWaypoints();
	
	if(transition == TRANS_ENTERING){
		_rightKnob.setAntiBounce(antiBounceSlow);
		
		_vfd.clearScreen();
		_vfd.setFont(VFD::FONT_5x7) ;
		_vfd.setCursor(0,10);
		_vfd.write("Waypoints");
		
		// safety check
		if(_lineOffset >=  wps.size())
			_lineOffset = 0;
		
		lastOffset = INT_MAX;
		firstLine = 0;
		needsRedraw = true;
	}
	
	// check for change in gps offsets ?
	// if anything changed, needsRedraw = true
	
	if( lastOffset != _lineOffset){
		lastOffset = _lineOffset;
		needsRedraw = true;
	}
	
	if(needsRedraw){
		needsRedraw = false;
		
		GPSLocation_t here;
		here.isValid = false;
		here.altitudeIsValid = false;
		gps->GetLocation(here);
 
 		vector<vector<string>> rows = {};

		size_t totalLines = wps.size() + 1;  // add kEXIT and kNEW_WAYPOINT
		
		if(_lineOffset > totalLines -1)
			_lineOffset = totalLines -1;
		
		// framer code
		if( (_lineOffset - displayedLines + 1) > firstLine) {
			firstLine = _lineOffset - displayedLines + 1;
		}
		else if(_lineOffset < firstLine) {
			firstLine = max(firstLine - 1,  0);
		}
		
		// create lines
		for(auto i = 0 ; i < totalLines; i++){
			
			stringvector row = {};
			
			string line = "";
			string distance  = "";

			bool isSelected = i == _lineOffset;
			
			if(i < wps.size()) {
				auto wp = wps[i];
				string name = wp.name;
 
				std::transform(name.begin(), name.end(),name.begin(), ::toupper);
				line = string("\x1d") + (isSelected?"\xb9":" ") + string("\x1c ") +  name;
				
				if(here.isValid){
				  auto r = GPSmgr::dist_bearing(here,wp.location);
					distance = distanceString(r.first * 0.6213711922);
					std::transform(distance.begin(), distance.end(),distance.begin(), ::toupper);
	 			}
 			}
			else {
				line = string("\x1d") + (isSelected?"\xb9":" ") + string("\x1c ") + " EXIT" ;
			}
	 
			row = {line,distance};
 			rows.push_back(row);
		}
		
 		_vfd.printRows(20, 9, rows, firstLine, displayedLines, VFD::FONT_MINI);
		
		if(rows.size() > displayedLines){
			
			float bar_height =  (float)(displayedLines +1)/ (float)rows.size() ;
			float offset =  (float)_lineOffset / ((float)rows.size() -1) ;
			
			_vfd.drawScrollBar(11, bar_height ,offset);
		}
	}
	
	drawTimeBox();
}


bool DisplayMgr::processSelectorKnobActionForGPSWaypoint( knob_action_t action){
	bool wasHandled = false;
	
	
	string uuid = "";
	
	PiCarMgr*	mgr 	= PiCarMgr::shared();
	auto wps 	= mgr->getWaypoints();
	
	if(_lineOffset < wps.size()){
		auto wp = wps[_lineOffset];
		uuid = wp.uuid;
	}
	
	auto savedCB = _wayPointCB;
	
	if(action == KNOB_CLICK ||  action == KNOB_DOUBLE_CLICK){
		
		popMode();
		_wayPointCB = NULL;
		_lineOffset = 0;
		wasHandled = true;
		
		if(savedCB) {
			savedCB(wasHandled, uuid, action);
		}
	}
	
	return wasHandled;
}

void DisplayMgr::drawGPSWaypointScreen(modeTransition_t transition){
	
	PiCarMgr*		mgr 	= PiCarMgr::shared();
	GPSmgr*			gps 	= mgr->gps();
	
	uint8_t width = _vfd.width();
	uint8_t height = _vfd.height();
	
	uint8_t midX = width/2;
	static int	last_heading = INT_MAX;
	
	if(transition == TRANS_LEAVING) {
		_rightKnob.setAntiBounce(antiBounceDefault);
		_vfd.clearScreen();
		return;
	}
	
	if(transition == TRANS_ENTERING){
		_rightKnob.setAntiBounce(antiBounceSlow);
		_vfd.clearScreen();
		last_heading = INT_MAX;
	}
	
	// find waypoint with uuid
	auto wps = mgr->getWaypoints();
	
	if(_lineOffset < wps.size()){
		auto wp = wps[_lineOffset];
		string name = wp.name;
		
		if(name.size() > 12){
			std::transform(name.begin(), name.end(),name.begin(), ::toupper);
			name = truncate(name, 22);
			_vfd.setCursor(0,8);
			_vfd.setFont(VFD::FONT_MINI);
		}
		else{
			_vfd.setCursor(0,10);
			_vfd.setFont(VFD::FONT_5x7);
		}
		
		_vfd.printPacket("%s", name.c_str());
		
		uint8_t col = 0;
		uint8_t topRow = 22;
		
		_vfd.setFont(VFD::FONT_MINI);
		_vfd.setCursor(2,topRow);
		_vfd.printPacket("DISTANCE");
		
		_vfd.setCursor(midX+20 ,topRow);
		_vfd.printPacket("BEARING");
		
		_vfd.setFont(VFD::FONT_5x7) ;
		
		GPSLocation_t here;
		GPSVelocity_t velocity;
		if(gps->GetLocation(here) & here.isValid){
			auto r = GPSmgr::dist_bearing(here,wp.location);
			
			_vfd.setCursor(col+10, topRow+10 );
			
			_vfd.printPacket("%-6s",  distanceString(r.first * 0.6213711922).c_str());
			//		_vfd.printPacket("%6.2fmi", r.first * 0.6213711922);
			
			int bearing = int(r.second);
			
			string ordinal[] =  {"N ","NE","E ", "SE","S ","SW","W ","NW"} ;
			string dir = ordinal[int(floor((bearing / 45) + 0.5)) % 8]  ;
			
			_vfd.setCursor(midX+25 ,topRow+10);
			_vfd.printPacket("%3d\xa0\x1c%2s\x1d ", bearing, dir.c_str());
			
			int heading = INT_MAX;
			
			if(gps->GetVelocity(velocity) && velocity.isValid){
				//save heading
				last_heading  = int(velocity.heading);
				heading =  int( r.second - velocity.heading );
				
				//				printf("r: %5.1f vel: %5.1f = %d\n", r.second,  velocity.heading,  heading);
				
			}
			else if( last_heading != INT_MAX){
				heading =  int( r.second - last_heading );
			}
			
			if( heading != INT_MAX){
				_vfd.setCursor(col+10,topRow+22);
				_vfd.printPacket("%2s %3d\xa0 %2s",heading<0?"<-":"", abs(heading), heading>0?"->":"");
			}
		}
		
		string utm = GPSmgr::UTMString(wp.location);
		vector<string> v = split<string>(utm, " ");
		
		_vfd.setFont(VFD::FONT_MINI);
		_vfd.setCursor(2,height -10);
		_vfd.printPacket("UTM:");
		
		_vfd.setFont(VFD::FONT_5x7) ;
		_vfd.setCursor(0, height );
		_vfd.printPacket(" %-18s", utm.c_str());
	}
	
	drawTimeBox();
}


// MARK: -  Scanner Channels


void DisplayMgr::showChannel( RadioMgr::channel_t channel,
									  showChannelCallBack_t cb) {
	if(channel.first != RadioMgr::MODE_UNKNOWN){
		_currentChannel = channel;
		_showChannelCB = cb;
		setEvent(EVT_PUSH, MODE_CHANNEL_INFO);
	}
}


bool DisplayMgr::processSelectorKnobActionForChannelInfo( knob_action_t action){
	bool wasHandled = false;
	
	
	string uuid = "";
	
	auto savedCB = _showChannelCB;
	auto savedChannel = _currentChannel;
	
	if(action == KNOB_CLICK ||  action == KNOB_DOUBLE_CLICK){
		
		popMode();
		_showChannelCB = NULL;
		_currentChannel = {RadioMgr::MODE_UNKNOWN, 0};
		wasHandled = true;
		
		if(savedCB) {
			savedCB(wasHandled, savedChannel, action);
		}
	}
	
	return wasHandled;
}


void DisplayMgr::drawChannelInfo(modeTransition_t transition){
	
	PiCarMgr*		mgr 	= PiCarMgr::shared();
	
	//	uint8_t width = _vfd.width();
	//	uint8_t height = _vfd.height();
	
	int centerY = _vfd.height() /2;
	
	if(transition == TRANS_LEAVING) {
		_rightKnob.setAntiBounce(antiBounceDefault);
		_vfd.clearScreen();
		return;
	}
	
	
	RadioMgr::radio_mode_t  mode = _currentChannel.first;
	uint32_t						freq = _currentChannel.second;
	
	
	if(transition == TRANS_ENTERING){
		_rightKnob.setAntiBounce(antiBounceSlow);
		_vfd.clearScreen();
		
		_vfd.setCursor(0,7);
		_vfd.setFont(VFD::FONT_MINI);
		_vfd.printPacket("CHANNEL INFO");
		
		_vfd.setFont(VFD::FONT_5x7);
		
		constexpr int maxLen = 20;
		string spaces(maxLen, ' ');
		
		string titleStr = "";
		PiCarMgr::station_info_t info;
		if(mgr->getStationInfo(mode, freq, info)){
			titleStr = truncate(info.title, maxLen);
			string portionOfSpaces = spaces.substr(0, (maxLen - titleStr.size()) / 2);
			titleStr = portionOfSpaces + titleStr;
		}
		
		_vfd.setCursor(0,centerY-5);
		_vfd.printPacket("%-21s",titleStr.c_str() );
		
		string channelStr = RadioMgr::modeString(mode) + " "
		+ RadioMgr::hertz_to_string(freq, 3) + " "
		+ RadioMgr::freqSuffixString(freq);
		
		string portionOfSpaces = spaces.substr(0, (maxLen - channelStr.size()) / 2);
		channelStr = portionOfSpaces + channelStr;
		_vfd.setCursor(0,centerY+5);
		_vfd.printPacket("%-21s",channelStr.c_str() );
		
		_vfd.setCursor(0, 60);
		_vfd.printPacket("           ");
		
		bool isPreset = mgr->isPresetChannel(mode, freq);
		bool isScanner = mgr->isScannerChannel(mode, freq);
		_vfd.setCursor(0, 60);
		_vfd.setFont(VFD::FONT_MINI);
		_vfd.printPacket("%s %s", isPreset?"PRESET":"", isScanner?"SCANNER":"");
	}
	
	drawTimeBox();
	
	
}


void DisplayMgr::showScannerChannels( RadioMgr::channel_t initialChannel,
												 time_t timeout ,
												 showScannerChannelsCallBack_t cb){
	_lineOffset = 0;
	
	// set _lineOffset to proper entry
	if (initialChannel.first  != RadioMgr::MODE_UNKNOWN){
		PiCarMgr*		mgr 	= PiCarMgr::shared();
		auto channels 	= mgr->getScannerChannels();
		
		for( int i = 0; i < channels.size(); i++){
			if(channels[i].first == initialChannel.first
				&& channels[i].second == initialChannel.second){
				_lineOffset = i;
				break;
			}
		}
	}
	
	_scannnerChannelsCB = cb;
	_menuTimeout = timeout;
	setEvent(EVT_PUSH, MODE_SCANNER_CHANNELS);
}


bool DisplayMgr::processSelectorKnobActionForScannerChannels( knob_action_t action){
	bool wasHandled = false;
	
	
	switch(action){
			
		case KNOB_EXIT:
			if(_scannnerChannelsCB) {
				_scannnerChannelsCB(false, {RadioMgr::MODE_UNKNOWN, 0}, action);
			}
			setEvent(EVT_POP, MODE_UNKNOWN);
			_scannnerChannelsCB = NULL;
			_lineOffset = 0;
			break;
			
		case KNOB_UP:
			if(_lineOffset < 255){
				_lineOffset++;
				setEvent(EVT_NONE,MODE_SCANNER_CHANNELS);
			}
			wasHandled = true;
			break;
			
		case KNOB_DOWN:
			if(_lineOffset != 0) {
				_lineOffset--;
				setEvent(EVT_NONE,MODE_SCANNER_CHANNELS);
			}
			wasHandled = true;
			break;
			
		case KNOB_CLICK:
		case KNOB_DOUBLE_CLICK:
		{
			PiCarMgr*	mgr 	= PiCarMgr::shared();
			auto channels = mgr->getScannerChannels();
			bool success = false;
			
			RadioMgr::channel_t channel = {RadioMgr::MODE_UNKNOWN, 0};
			
			auto savedCB = _scannnerChannelsCB;
			
			if(_lineOffset < channels.size()) {
				channel = channels[_lineOffset];
				success = true;
			};
			
			popMode();
			_wayPointCB = NULL;
			_lineOffset = 0;
			
			if(savedCB) {
				savedCB(success, channel, action);
			}
			
			if(!success)
				setEvent(EVT_REDRAW, _current_mode);
			
			wasHandled = true;
			
		}
			break;
			
		default: break;
			
	}
	
	if(action ==  KNOB_UP || action == KNOB_DOWN){
		PiCarMgr*	mgr 	= PiCarMgr::shared();
		auto channels = mgr->getScannerChannels();
		
		if(_lineOffset < channels.size()) {
			RadioMgr::channel_t channel = channels[_lineOffset];
			if(_scannnerChannelsCB) (_scannnerChannelsCB)(true, channel, KNOB_SELECTING);
		}
		
	}
	
	return wasHandled;
}


void DisplayMgr::drawScannerChannels(modeTransition_t transition){
	
	PiCarMgr*		mgr 	= PiCarMgr::shared();
	
	constexpr int displayedLines = 5;
	
	static int lastOffset = 0;
	static int firstLine = 0;
	
	bool needsRedraw = false;
	
	if(transition == TRANS_LEAVING) {
		
		_rightKnob.setAntiBounce(antiBounceDefault);
		_vfd.clearScreen();
		lastOffset = 0;
		firstLine = 0;
		return;
	}
	
	auto channels = mgr->getScannerChannels();
	
	if(transition == TRANS_ENTERING){
		_rightKnob.setAntiBounce(antiBounceSlow);
		
		_vfd.clearScreen();
		_vfd.setFont(VFD::FONT_5x7) ;
		_vfd.setCursor(0,10);
		_vfd.write("Scanner");
		
		// safety check
		if(_lineOffset >=  channels.size())
			_lineOffset = 0;
		
		lastOffset = INT_MAX;
		firstLine = 0;
		needsRedraw = true;
		
	}
	
	// check for change in gps offsets ?
	// if anything changed, needsRedraw = true
	
	if( lastOffset != _lineOffset){
		lastOffset = _lineOffset;
		needsRedraw = true;
	}
	
	if(needsRedraw){
		needsRedraw = false;
		
		vector<string> lines = {};
		size_t totalLines = channels.size() + 1;  // add kEXIT
		
		if(_lineOffset > totalLines -1)
			_lineOffset = totalLines -1;
		
		// framer code
		if( (_lineOffset - displayedLines + 1) > firstLine) {
			firstLine = _lineOffset - displayedLines + 1;
		}
		else if(_lineOffset < firstLine) {
			firstLine = max(firstLine - 1,  0);
		}
		
		// create lines
		for(auto i = 0 ; i < totalLines; i++){
			
			string line = "";
			bool isSelected = i == _lineOffset;
			
			if(i < channels.size()) {
				RadioMgr::radio_mode_t  mode = channels[i].first;
				uint32_t freq = channels[i].second;
				
				string channelStr = RadioMgr::hertz_to_string(freq, 3);
				
				PiCarMgr::station_info_t info;
				if(mgr->getStationInfo(mode, freq, info)){
					string title = truncate(info.title,  20);
					channelStr += + " " + title;
				}
				
				std::transform(channelStr.begin(), channelStr.end(),channelStr.begin(), ::toupper);
				line = string("\x1d") + (isSelected?"\xb9":" ") + string("\x1c ") +  channelStr;
			}
			else {
				line = string("\x1d") + (isSelected?"\xb9":" ") + string("\x1c ") + " EXIT" ;
			}
			
			lines.push_back(line);
		}
		
		_vfd.setFont(VFD::FONT_MINI) ;
		_vfd.printLines(20, 9, lines, firstLine, displayedLines, VFD::FONT_MINI);
		
		if(lines.size() > displayedLines){
			
			float bar_height =  (float)(displayedLines +1)/ (float)lines.size() ;
			float offset =  (float)_lineOffset / ((float)lines.size() -1) ;
			
			_vfd.drawScrollBar(11, bar_height ,offset);
		}
		
	}
	
	drawTimeBox();
}



// MARK: -  Display value formatting

bool DisplayMgr::normalizeCANvalue(string key, string & valueOut){
	
	FrameDB*	fDB 	= PiCarMgr::shared()->can()->frameDB();
	
	string rawValue;
	string value;
	
	char buffer[256];
	char *p = buffer;
	
	if(fDB->valueWithKey(key, &rawValue)) {
		
		switch(fDB->unitsForKey(key)){
				
			case FrameDB::DEGREES_C:
			{
				double cTemp = fDB->normalizedDoubleForValue(key,rawValue);
				double fTemp =  cTemp *(9.0/5.0) + 32.0;
				sprintf(p, "%d\xA0" "F",  (int) round(fTemp));
				value = string(buffer);
			}
				break;
				
			case FrameDB::KPA:
			{
				double kPas = fDB->normalizedDoubleForValue(key,rawValue);
				double psi =  kPas * 0.1450377377;
				sprintf(p, "%2d psi",   (int) round(psi));
				value = string(buffer);
			}
				break;
				
			case	FrameDB::VOLTS:
			{
				float volts =   stof(rawValue);
				sprintf(p, "%2.2f V",  volts);
				value = string(buffer);
			}
				break;
				
			case	FrameDB::KM:
			{
				float miles = (stof(rawValue) / 10) *  0.6213712;
				sprintf(p, "%2.1f",  miles);
				value = string(buffer);
			}
				break;
				
			case	FrameDB::KPH:
			{
				float mph = stof(rawValue) *  0.6213712;
				sprintf(p, "%2d mph",  (int) round(mph));
				value = string(buffer);
			}
				break;
				
				
			case FrameDB::FUEL_TRIM:{
				double trim = fDB->normalizedDoubleForValue(key,rawValue);
				sprintf(p, "%1.1f%%",  trim);
				value = string(buffer);
			}
				break;
				
			case FrameDB::PERCENT:{
				double pc = fDB->normalizedDoubleForValue(key,rawValue);
				sprintf(p, "%d%%",  int(pc));
				value = string(buffer);
			}
				break;
				
				
			case FrameDB::LPH:
			{
				double lph = fDB->normalizedDoubleForValue(key,rawValue);
				double gph =  lph * 0.2642;
				sprintf(p, "%1.1f gph",  gph);
				value = string(buffer);
			}
				break;
				
				
			default:
				value = rawValue;
		}
		
	}
	
	if(value.empty()){
		value = "---";
	}
	
	valueOut = value;
	
	//
	//	if(key == "GM_COOLANT_TEMP")
	//			value = "210\xA0";
	//	else 	if(key == "GM_TRANS_TEMP")
	//		value = "188\xA0";
	//	else 	if(key == "GM_OIL_PRESSURE")
	//		value = "48";
	//	else 	if(key == "LONG_FUEL_TRIM_1")
	//		value = "10.0";
	//	else 	if(key == "LONG_FUEL_TRIM_2")
	//		value = "7.2";
	//	else 	if(key == "RUN_TIME")
	//		value = "7:43";
	// else
	//	 value = "---";
	
	return true;
	
}

// MARK: -  MetaData reader
inline static const char kPadCharacter = '=';

vector<uint8_t> decode(const std::string& input) {
	if(input.empty())
		return {};
	
	if(input.length() % 4)
		throw std::runtime_error("Invalid base64 length!");
	
	std::size_t padding{};
	
	if(input.length())
	{
		if(input[input.length() - 1] == kPadCharacter) padding++;
		if(input[input.length() - 2] == kPadCharacter) padding++;
	}
	
	std::vector<uint8_t> decoded;
	decoded.reserve(((input.length() / 4) * 3) - padding);
	
	std::uint32_t temp{};
	auto it = input.begin();
	
	while(it < input.end())
	{
		for(std::size_t i = 0; i < 4; ++i)
		{
			temp <<= 6;
			if     (*it >= 0x41 && *it <= 0x5A) temp |= *it - 0x41;
			else if(*it >= 0x61 && *it <= 0x7A) temp |= *it - 0x47;
			else if(*it >= 0x30 && *it <= 0x39) temp |= *it + 0x04;
			else if(*it == 0x2B)                temp |= 0x3E;
			else if(*it == 0x2F)                temp |= 0x3F;
			else if(*it == kPadCharacter)
			{
				switch(input.end() - it)
				{
					case 1:
						decoded.push_back((temp >> 16) & 0x000000FF);
						decoded.push_back((temp >> 8 ) & 0x000000FF);
						return decoded;
					case 2:
						decoded.push_back((temp >> 10) & 0x000000FF);
						return decoded;
					default:
						throw std::runtime_error("Invalid padding in base64!");
				}
			}
			else throw std::runtime_error("Invalid character in base64!");
			
			++it;
		}
		
		decoded.push_back((temp >> 16) & 0x000000FF);
		decoded.push_back((temp >> 8 ) & 0x000000FF);
		decoded.push_back((temp      ) & 0x000000FF);
	}
	
	return decoded;
}


void  DisplayMgr::clearAPMetaData() {
	pthread_mutex_lock (&_apmetadata_mutex);
	_airplayMetaData.clear();
	pthread_mutex_unlock(&_apmetadata_mutex);
	
	//	printf("META cleared\n");
	
};

void DisplayMgr::airplayStarted(){
	RadioMgr*	radio 	= PiCarMgr::shared()->radio();
	
	if(radio->isOn() && _shouldAutoPlay){
		if(radio->radioMode() != RadioMgr::AIRPLAY){
			radio->setFrequencyandMode(RadioMgr::AIRPLAY, 1);
		}
		
		_shouldAutoPlay = false;
	}
}

void DisplayMgr::processAirplayMetaData(string type, string code, vector<uint8_t> payload ){
	
	//	  	printf("processAirplayMetaData( %s %s %lu)\n",type.c_str(),code.c_str(),payload.size());
	
	//	RadioMgr*	radio 	= PiCarMgr::shared()->radio();
	
	static map<string, string>  airplaycache = {};
	static bool session_started = false;
	
	//	if(radio->isOn()){
	
	if(type == "core"){
		
		//			  {'core', 'asal'}, // daap.songalbum
		//			  {'core', 'asar'},	// daap.songartist
		//			  {'core', 'minm'}, // dmap.itemname
		//		  //	{'core', 'asgn'}, //  daap.songgenre
		//		  //	{'core', 'ascp'}, //  daap.daap.songcomposer
		//		  //	{'core', 'asdk'}, //  daap.daap.songdatakind
		//			  {'core', 'caps'}, // play status  ( 01/ 02 )
		
		static stringvector filter_table = {
			"asal" , // daap.songalbum
			"asar",	// daap.songartist
			"minm"  // dmap.itemname
		};
		
		if(session_started){
			if ( std::find(filter_table.begin(), filter_table.end(), code) != filter_table.end() ){
				string str =  string(payload.begin(), payload.end());
				airplaycache[code] = str;
				//					printf("META %s: %s\n",code.c_str(), str.c_str());
				return;
			}
		}
		
		if(code ==  "caps" ) {
			_airplayStatus = payload[0];
			clock_gettime(CLOCK_MONOTONIC, &_lastAirplayStatusTime);
			
			// play status
			//				printf("META play status %02x \n", _airplayStatus) ;
			showAirplayChange();
		}
		else  {
			printf("META %s,%s %zu  \n",type.c_str(),  code.c_str(), payload.size());
		}
	}
	else if(type == "ssnc"){
		//		{'ssnc', 'mden'}, //  Metadata stream processing end
		//		{'ssnc', 'mdst'}, //  Metadata stream processing start
		
		if(code ==  "mdst" ) {
			airplaycache.clear();
			session_started = true;
		}
		else 	if(code ==  "pbeg" ) {
			// play stream begin.
			airplayStarted();
		}
		else if(code ==  "pend" || code ==  "aend" ){
			// airplay disconnected
			session_started = false;
			_airplayStatus = 0;
			clearAPMetaData();
			showAirplayChange();
			
			//	 		printf("META airplay diconnected\n") ;
		}
		else 	if(code ==  "mden" ) {
			// udate the airplay info.
			pthread_mutex_lock (&_apmetadata_mutex);
			_airplayMetaData.clear();
			_airplayMetaData = airplaycache;
			pthread_mutex_unlock(&_apmetadata_mutex);
			
			showAirplayChange();
			airplaycache.clear();
			session_started = false;
		}
	}
	
	//	}
}

typedef struct {
	uint32_t type;
	uint32_t code;
} filter_table_t;

static filter_table_t filter_table[] = {
	{'core', 'asal'}, // daap.songalbum
	{'core', 'asar'},	// daap.songartist
	{'core', 'minm'}, // dmap.itemname
	//	{'core', 'asgn'}, //  daap.songgenre
	//	{'core', 'ascp'}, //  daap.daap.songcomposer
	//	{'core', 'asdk'}, //  daap.daap.songdatakind
	{'core', 'caps'}, // play status  ( 01/ 02 )
	
	{'ssnc', 'mden'}, //  Metadata stream processing end
	{'ssnc', 'mdst'}, //  Metadata stream processing start
	
	{'ssnc', 'aend'},	// airplay session end
	{'ssnc', 'abeg'},	// airplay session begin
	
	{'ssnc', 'pbeg'},	// play stream begin.
	{'ssnc', 'pend'}, // play stream end.
	
};
static bool sInFilterTable(uint32_t type, uint32_t code){
	
	for( int i = 0; i <  sizeof(filter_table)/ sizeof(filter_table_t); i++){
		if(filter_table[i].code == code && filter_table[i].type == type)
			return true;
	};
	
	return false;
}


void DisplayMgr::MetaDataReaderLoop(){
	
	PRINT_CLASS_TID;
	
	
	string 				metaDataFilePath  = "/tmp/shairport-sync-metadata";
	std::ifstream		ifs;
	
	while(_isRunning){
		
		// if not setup // check back later
		if(!_isSetup){
			sleep(2);
			continue;
		}
		
		try{
			
			if(!ifs.is_open()){
				ifs.open(metaDataFilePath, ios::in);
				
				if(!ifs.is_open()) {
					sleep(1);
					continue;
				}
			}
			
			string line;
			
			while ( std::getline(ifs, line) ) {
				
				uint32_t type, code;
				uint32_t length;
				
				int ret = sscanf(line.c_str(),"<item><type>%8x</type><code>%8x</code><length>%u</length>",&type,&code,&length);
				if (ret==3) {
					
					bool shouldProcessPacket  = sInFilterTable( type, code);
					char typestring[5] = {0};
					char codestring[5] = {0};
					string payload = "";
					
					*(uint32_t*)typestring = htonl(type);
					*(uint32_t*)codestring = htonl(code);
					
					if(length && std::getline(ifs, line) ){
						if(line == "<data encoding=\"base64\">") {
							
							if(std::getline(ifs, line) ){
								
								if(shouldProcessPacket){
									
									auto input_length = line.find("</data>");
									if(input_length != std::string::npos){
										
										payload = line.substr(0,input_length);
										payload = Utils::trimCNTRL(payload);
									}
								}
							}
						}
					}
					
					// filter out only the packets I want
					if(shouldProcessPacket){
						processAirplayMetaData(typestring, codestring,  decode(payload));
						
						//					printf("processed %s %s %s \n",typestring, codestring, payload.c_str());
					}
					
				}
				if(_isSetup && _isRunning)
					break;
			}
			
		}
		catch(std::ifstream::failure &err) {
			printf("MetaDataReader:FAIL: %s", err.what());
			if(ifs.is_open()) {
				ifs.close();
			}
		}
		
	}
	
	if(ifs.is_open()) {
		ifs.close();
	}
};


void* DisplayMgr::MetaDataReaderThread(void *context){
	DisplayMgr* d = (DisplayMgr*)context;
	
	//   the pthread_cleanup_push needs to be balanced with pthread_cleanup_pop
	pthread_cleanup_push(   &DisplayMgr::MetaDataReaderThreadCleanup ,context);
	
	d->MetaDataReaderLoop();
	
	pthread_exit(NULL);
	
	pthread_cleanup_pop(0);
	return((void *)1);
}


void DisplayMgr::MetaDataReaderThreadCleanup(void *context){
	//GPSmgr* d = (GPSmgr*)context;
	
	printf("cleanup GPSReader\n");
}

