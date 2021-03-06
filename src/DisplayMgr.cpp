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
#include <sys/utsname.h>

#include "Utils.hpp"
#include "XXHash32.h"
#include "timespec_util.h"

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

static constexpr uint8_t VFD_OUTLINE = 0x14;
static constexpr uint8_t VFD_CLEAR_AREA = 0x12;
static constexpr uint8_t VFD_SET_AREA = 0x11;
static constexpr uint8_t VFD_SET_CURSOR = 0x10;
static constexpr uint8_t VFD_SET_WRITEMODE = 0x1A;


DisplayMgr::DisplayMgr(){
	_eventQueue = {};
	_ledEvent = 0;
	_isSetup = false;
	_isRunning = true;
	_dimLevel = 1.0;
	
	pthread_create(&_updateTID, NULL,
						(THREADFUNCPTR) &DisplayMgr::DisplayUpdateThread, (void*)this);
	
}

DisplayMgr::~DisplayMgr(){
	stop();
	_isRunning = false;
	pthread_cond_signal(&_cond);
	pthread_join(_updateTID, NULL);
	
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
		
//		setKnobColor(KNOB_RIGHT, RGB::Lime);
//		setKnobColor(KNOB_LEFT, RGB::Lime);
		
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
		
		resetMenu();
		showStartup();
	}
	
	return _isSetup;
}


void DisplayMgr::stop(){
	
	if(_isSetup){
		
		if(_menuCB) _menuCB(false, 0, KNOB_EXIT);
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


void DisplayMgr::ledEventUpdate(){
	
	if( _ledEvent & (LED_EVENT_STOP)){
		ledEventSet(0, LED_EVENT_STOP);
		
		// dim it then restore
		//		uint8_t sav =  _leftRing.GlobalCurrent();
		_leftRing.clearAll();
		//		_leftRing.SetGlobalCurrent(sav);
	}
	
	if( _ledEvent & (LED_EVENT_STARTUP | LED_EVENT_STARTUP_RUNNING))
		runLEDEventStartup();
	
	
	if( _ledEvent & (LED_EVENT_VOL | LED_EVENT_VOL_RUNNING))
		runLEDEventVol();
	
	if( _ledEvent & (LED_EVENT_MUTE | LED_EVENT_MUTE_RUNNING))
		runLEDEventMute();
}


void DisplayMgr::ledEventSet(uint32_t set, uint32_t reset){
	pthread_mutex_lock (&_mutex);
	_ledEvent &= ~reset;
	_ledEvent |= set;
	pthread_mutex_unlock (&_mutex);
	pthread_cond_signal(&_cond);
}

void DisplayMgr::runLEDEventStartup(){
	
	static uint8_t 	ledStep = 0;
	
	if( _ledEvent & LED_EVENT_STARTUP ){
		ledEventSet(LED_EVENT_STARTUP_RUNNING,LED_EVENT_ALL );
		
		ledStep = 0;
		//		printf("\nLED STARTUP\n");
		_leftRing.clearAll();
		_rightRing.clearAll();
	}
	else if( _ledEvent & LED_EVENT_STARTUP_RUNNING ){
		
		if(ledStep < 24 * 4){
			
#if 1
			DuppaLEDRing::led_block_t data = {{0,0,0}};
			data[mod(++ledStep, 24)] = {255,255,255};
			_leftRing.setLEDs(data);
			_rightRing.setLEDs(data);

#else
			_leftRing.setColor( mod(ledStep, 24), 0, 0, 0);
			ledStep++;
			_leftRing.setColor(mod(ledStep, 24), 255, 255, 255);
#endif
			
		}
		else {
			ledEventSet(0, LED_EVENT_STARTUP_RUNNING);
			_leftRing.clearAll();
			_rightRing.clearAll();
			//			printf("\nLED RUN DONE\n");
			
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
		ledEventSet(LED_EVENT_MUTE_RUNNING, LED_EVENT_ALL);
	}
	
	// do the first cycle right away
	if( _ledEvent & LED_EVENT_MUTE_RUNNING ){
		
		struct timespec now, diff;
		clock_gettime(CLOCK_MONOTONIC, &now);
		timespec_sub( &diff, &now, &lastEvent);
		
		uint64_t diff_millis = timespec_to_msec(&diff);
		
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
	
	if( _ledEvent & LED_EVENT_VOL ){
		clock_gettime(CLOCK_MONOTONIC, &startedEvent);
		ledEventSet(LED_EVENT_VOL_RUNNING,LED_EVENT_ALL );
		
		//	 	printf("\nVOL STARTUP\n");
	}
	else if( _ledEvent & LED_EVENT_VOL_RUNNING ){
		
		struct timespec now, diff;
		clock_gettime(CLOCK_MONOTONIC, &now);
		timespec_sub( &diff, &now, &startedEvent);
	
		if(diff.tv_sec <  1){
			
			float volume =  audio->volume();
			// volume LED scales between 1 and 24
			int ledvol = volume*23;
			for (int i = 0 ; i < 24; i++) {
				_leftRing.setGREEN(i, i <= ledvol?0xff:0 );
			}
			//			printf("\nVOL RUN\n");
			
		}
		else {
			ledEventSet(0, LED_EVENT_VOL_RUNNING);
			
			// scan the LEDS off
			for (int i = 0; i < 24; i++) {
				_leftRing.setColor( i, 0, 0, 0);
				usleep(10 * 1000);
			}
			
			//		printf("\nVOL RUN DONE\n");
			
		}
	}
	
}



// MARK: -  display tools


static uint8_t calculateRingCurrent(uint8_t level) {
	uint8_t current = DuppaLEDRing::maxGlobalCurrent();
	
	level = min(static_cast<int>(level), 7);
	
	uint8_t table[] = {1, 1, 2, 4, 5,  6, 8, DuppaLEDRing::maxGlobalCurrent()};
	
	current = table[level];
	
	return current;
	
}


bool DisplayMgr::setBrightness(double level) {
	
	bool success = false;
	
	if(_isSetup){
		_dimLevel = level;
		
		// vfd 0 -7
		uint8_t vfdLevel =  level * 7.0 ;
		
		if(vfdLevel == 0) vfdLevel  = 1;		
		success = _vfd.setBrightness(vfdLevel);
		
		uint8_t ledCurrent = calculateRingCurrent(level);
//
//		uint8_t ledCurrent = DuppaLEDRing::maxGlobalCurrent() * level;
//		ledCurrent = min(static_cast<int>( ledCurrent), static_cast<int>(DuppaLEDRing::maxGlobalCurrent()));
		_rightRing.SetGlobalCurrent(ledCurrent);
		_leftRing.SetGlobalCurrent(ledCurrent);
		
		_rightKnob.setBrightness(level);
		_leftKnob.setBrightness(level);

	}
	
	return success;
}

bool DisplayMgr::setKnobBackLight(bool isOn){
	_backlightKnobs = isOn;
	
	switch (_current_mode) {
		case MODE_TIME:
		case MODE_RADIO:
		
			if(_backlightKnobs){
				setKnobColor(KNOB_RIGHT, RGB::Lime);
				setKnobColor(KNOB_LEFT, RGB::Lime);
			}
			else {
				setKnobColor(KNOB_RIGHT, RGB::Black);
				setKnobColor(KNOB_LEFT, RGB::Black);
			}

			
		default: ;
			 
	}
	return true;
}



bool DisplayMgr::setKnobColor(knob_id_t knob, RGB color){
	bool success = false;
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


void DisplayMgr::showInfo(){
	setEvent(EVT_PUSH, MODE_INFO);
}


void DisplayMgr::showSettings(){
	setEvent(EVT_PUSH, MODE_SETTINGS);
}


void DisplayMgr::showDevStatus(){
	setEvent(EVT_PUSH, MODE_DEV_STATUS );
}


void DisplayMgr::showDimmerChange(){
	setEvent(EVT_PUSH, MODE_DIMMER );
}

void DisplayMgr::showBalanceChange(){
	setEvent(EVT_PUSH, MODE_BALANCE );
}

void DisplayMgr::showFaderChange(){
	setEvent(EVT_PUSH, MODE_FADER );
}

void DisplayMgr::showRadioChange(){
	setEvent(EVT_PUSH, MODE_RADIO );
}

void DisplayMgr::showDTC(){
	setEvent(EVT_PUSH, MODE_DTC);
}

void DisplayMgr::showDTCInfo(string code){
	setEvent(EVT_PUSH, MODE_DTC_INFO, code);
}


void DisplayMgr::showGPS(){
	setEvent(EVT_PUSH, MODE_GPS);
}

void DisplayMgr::showCANbus(uint8_t page){
	_currentPage = page;
	setEvent(EVT_PUSH, MODE_CANBUS);
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
		case MODE_BALANCE:
		case MODE_DIMMER:
		case MODE_FADER:
		case MODE_DTC:
		case MODE_DTC_INFO:
		case MODE_MENU:
			return true;
			
		default:
			return false;
	}
	
}


bool DisplayMgr::selectorKnobAction(knob_action_t action){
	
	bool wasHandled = false;
	//
	////	printf("selectorKnobAction (%d)\n", action);
	
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
				// no clue?
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
	
	switch (mode) {
		case MODE_CANBUS:
		{
			PiCarDB*		db 	= PiCarMgr::shared()->db();
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
		case MODE_BALANCE:
			wasHandled = processSelectorKnobActionForBalance(action);
			break;
			
		case MODE_FADER:
			wasHandled = processSelectorKnobActionForFader(action);
			break;
			
		case MODE_DIMMER:
			wasHandled = processSelectorKnobActionForDimmer(action);
			break;
			
		case MODE_DTC:
			wasHandled = processSelectorKnobActionForDTC(action);
			break;
			
		case MODE_DTC_INFO:
			wasHandled = processSelectorKnobActionForDTCInfo(action);
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
				
		}
		
	}
	
	return wasHandled;
}


void DisplayMgr::drawMenuScreen(modeTransition_t transition){
	
	//	uint8_t width = _vfd.width();
	uint8_t height = _vfd.height();
	
	uint8_t startV =  25;
	uint8_t lineHeight = 9;
	uint8_t maxLines =  (height - startV) / lineHeight ;
	//	uint8_t maxCol = width / 7;
	
	if(transition == TRANS_LEAVING) {
		_rightKnob.setAntiBounce(antiBounceDefault);
//		setKnobColor(KNOB_RIGHT, RGB::Lime);
		_vfd.clearScreen();
		return;
	}
	
	if(transition == TRANS_ENTERING) {
		_rightKnob.setAntiBounce(antiBounceSlow);
		setKnobColor(KNOB_RIGHT, RGB::Blue);
		_vfd.clearScreen();
		TRY(_vfd.setFont(VFD::FONT_5x7));
		TRY(_vfd.setCursor(20,10));
		
		auto title = _menuTitle;
		if (title.empty()) title = "Select";
		TRY(_vfd.write(title));
	}
	
	// did something change?
	if(transition == TRANS_ENTERING || transition == TRANS_REFRESH){
		
		if( (_currentMenuItem - maxLines) > _menuCursor) {
			_menuCursor = max(_currentMenuItem - maxLines, 0);
		}
		else if(_currentMenuItem < _menuCursor) {
			_menuCursor = max(_menuCursor - 1,  0);
		}
		
		uint8_t cursorV = startV;
		for(int i = _menuCursor; (i <= _menuCursor + maxLines) && (i < _menuItems.size()) ; i ++){
			char buffer[64] = {0};
			char moreIndicator =  ' ';
			
			auto lastLine = _menuCursor + maxLines;
			
			if(i == _menuCursor && _menuCursor != 0) moreIndicator = '<';
			else if( i == lastLine && lastLine != _menuItems.size() -1)  moreIndicator = '>';
			TRY(_vfd.setCursor(0,cursorV));
			sprintf(buffer, "%c%-18s %c",  i == _currentMenuItem?'\xb9':' ' , _menuItems[i].c_str(), moreIndicator);
			TRY(_vfd.write(buffer ));
			cursorV += lineHeight;
		}
	}
	
}

// MARK: -  mode utils

bool DisplayMgr::isStickyMode(mode_state_t md){
	bool isSticky = false;
	
	switch(md){
		case MODE_TIME:
		case MODE_RADIO:
		case MODE_SETTINGS:
		case MODE_GPS:
		case MODE_INFO:
		case MODE_CANBUS:
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
	_current_mode = _saved_mode==MODE_UNKNOWN ? MODE_TIME:_saved_mode;
	_saved_mode = MODE_UNKNOWN;
	
}

// MARK: -  DisplayUpdate thread

void DisplayMgr::DisplayUpdate(){
	
	//	printf("start DisplayUpdate\n");
	PRINT_CLASS_TID;
 
	pthread_condattr_t attr;
	pthread_condattr_init( &attr);

#if !defined(__APPLE__)
	//pthread_condattr_setclock is not supported on macOS
	pthread_condattr_setclock( &attr, CLOCK_MONOTONIC);
#endif
	pthread_cond_init( &_cond, &attr);
 
	while(_isRunning){
		
		// if not setup // check back later
		if(!_isSetup){
			usleep(10000);
			continue;
		}
		
		// --check if any events need processing else wait for a timeout
		struct timespec ts = {0, 0};
		clock_gettime(CLOCK_MONOTONIC, &ts);
		
		pthread_mutex_lock (&_mutex);
		// if there are LED events, run the update every half second
		// elese wait a whole second
		if(_ledEvent){
			ts.tv_sec += 0;
			ts.tv_nsec += 10.0e8;		// half second
		}
		else {
			ts.tv_sec += 1;
			ts.tv_nsec += 0;
		}
		bool shouldWait = ((_eventQueue.size() == 0)
								 && ((_ledEvent & 0x0000ffff) == 0));
		
		//
		if (shouldWait)
			pthread_cond_timedwait(&_cond, &_mutex, &ts);
		
		//		pthread_mutex_lock (&_mutex);
		eventQueueItem_t item = {EVT_NONE,MODE_UNKNOWN};
		if(_eventQueue.size()){
			item = _eventQueue.front();
			_eventQueue.pop();
		}
		
		mode_state_t lastMode = _current_mode;
		pthread_mutex_unlock (&_mutex);
		
		if(!_isRunning || !_isSetup)
			continue;
		
		// run the LED effects
		if(_ledEvent)
			ledEventUpdate();
		
		bool shouldRedraw = false;			// needs complete redraw
		bool shouldUpdate = false;			// needs update of data
		string eventArg = "";
		
		switch(item.evt){
				
				// timeout - nothing happened
			case EVT_NONE:
				struct timespec now, diff;
				clock_gettime(CLOCK_MONOTONIC, &now);
				timespec_sub( &diff, &now, &_lastEventTime);
			
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
				else if(_current_mode == MODE_BALANCE) {
					
					// check for {EVT_NONE,MODE_BALANCE}  which is a balance change
					if(item.mode == MODE_BALANCE) {
						clock_gettime(CLOCK_MONOTONIC, &_lastEventTime);
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
				else if(_current_mode == MODE_FADER) {
					
					// check for {EVT_NONE,MODE_FADER}  which is a fader change
					if(item.mode == MODE_FADER) {
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
		newState = MODE_RADIO;
	}else {
		newState = MODE_TIME;
	}
	
	
	return newState;
	
}


void* DisplayMgr::DisplayUpdateThread(void *context){
	DisplayMgr* d = (DisplayMgr*)context;
	
	//   the pthread_cleanup_push needs to be balanced with pthread_cleanup_pop
	pthread_cleanup_push(   &DisplayMgr::DisplayUpdateThreadCleanup ,context);
	
	d->DisplayUpdate();
	
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
				
			case MODE_DEV_STATUS:
				drawDeviceStatusScreen(transition);
				break;
				
			case MODE_TIME:
				drawTimeScreen(transition);
				break;
				
			case MODE_DIMMER:
				drawDimmerScreen(transition);
				break;
				
			case MODE_BALANCE:
				drawBalanceScreen(transition);
				break;
				
			case MODE_FADER:
				drawFaderScreen(transition);
				break;
				
			case MODE_RADIO:
				drawRadioScreen(transition);
				break;
				
			case MODE_SETTINGS:
				drawSettingsScreen(transition);
				break;
				
			case MODE_MENU:
				drawMenuScreen(transition);
				break;
				
			case MODE_GPS:
				drawGPSScreen(transition);
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
		
		setKnobColor(KNOB_RIGHT, RGB::Lime);
		setKnobColor(KNOB_LEFT, RGB::Lime);
		
			uint8_t leftbox 	= 5;
		uint8_t rightbox 	= width - 5;
		uint8_t topbox 	= 5 ;
		uint8_t bottombox = height - 5  ;
		
		_vfd.clearScreen();
		
		//draw box outline
		uint8_t buff1[] = {VFD_OUTLINE,leftbox,topbox,rightbox,bottombox };
		_vfd.writePacket(buff1, sizeof(buff1), 0);
			
		string str = "PiCar";
		auto start  =  centerX  -( (str.size() /2)  * 11) - 7 ;
		_vfd.setFont(VFD::FONT_10x14);
		_vfd.setCursor( start ,centerY+5);
		_vfd.write(str);
 
 		LEDeventStartup();
	}
 
	if(transition == TRANS_ENTERING || transition == TRANS_REFRESH){
		PiCarMgr*			mgr 	= PiCarMgr::shared();
		RadioMgr*			radio 	= mgr->radio();
		GPSmgr*				gps 		= mgr->gps();
		PiCarCAN*			can 		= mgr->can();
	
	 	if(radio->isConnected()){
	 		_vfd.setCursor( 15, 50);
			_vfd.setFont(VFD::FONT_MINI);
			_vfd.printPacket( "RADIO");
		}
		
		if(gps->isConnected()){
 			_vfd.setFont(VFD::FONT_MINI);
			_vfd.setCursor( 50, 50);
			_vfd.printPacket( "GPS");
		}
 
		if(can->isConnected()){
			_vfd.setFont(VFD::FONT_MINI);
			_vfd.setCursor( 80, 50);
			_vfd.printPacket( "CANBUS");
		}
	}

	if(transition == TRANS_LEAVING){
		setKnobColor(KNOB_RIGHT, RGB::Black);
		setKnobColor(KNOB_LEFT, RGB::Black);
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
	
}



void DisplayMgr::drawTimeScreen(modeTransition_t transition){
	
	PiCarDB*		db 	= PiCarMgr::shared()->db();
	
	
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	char buffer[128] = {0};
	
	if(transition == TRANS_LEAVING) {
		return;
	}
	
	if(transition == TRANS_ENTERING){
		_vfd.clearScreen();
 	}
	 
	if(_backlightKnobs){
		setKnobColor(KNOB_RIGHT, RGB::Lime);
		setKnobColor(KNOB_LEFT, RGB::Lime);
	}
	else {
		setKnobColor(KNOB_RIGHT, RGB::Black);
		setKnobColor(KNOB_LEFT, RGB::Black);
	}
		  //		_leftRing.clearAll();
	std::strftime(buffer, sizeof(buffer)-1, "%2l:%M:%S", t);
	
	_vfd.setCursor(10,35) ;
	_vfd.setFont(VFD::FONT_10x14) ;
	_vfd.write(buffer) ;
	
	_vfd.setFont(VFD::FONT_5x7) ;
	_vfd.write( (t->tm_hour > 12)?"PM":"AM");
	
	{
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
		
		buffer[0] = 0;
		char* p = &buffer[0];

		if(hasInside){
			p+=  sprintf(p, "%d\xa0%s", (int) round(fInside) ,  (hasOutside?"":"F") );
 		}

		if(hasOutside){
	 		p+=  sprintf(p, "%s%d\xa0" "F",  (hasInside?"/":""), (int) round(fOutside) );
		}
 
		if(hasInside || hasOutside){
			_vfd.setCursor( 10, 60)	;
			_vfd.setFont(VFD::FONT_5x7);
			_vfd.printPacket("%-10s", buffer);
		}
	}
	
 
	drawEngineCheck();
	
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
		uint8_t buff1[] = {VFD_OUTLINE,leftbox,topbox,rightbox,bottombox };
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
			VFD_CLEAR_AREA,
			// static_cast<uint8_t>(itemX+1),  static_cast<uint8_t> (topbox+1),
			static_cast<uint8_t>(start),  static_cast<uint8_t> (topbox+1),
			static_cast<uint8_t> (rightbox-1),static_cast<uint8_t> (bottombox-1)};
		
		_vfd.writePacket(buff2, sizeof(buff2), 1000);
	}
	
	// fill  area box
	uint8_t buff3[] = {VFD_SET_AREA,
		static_cast<uint8_t>(leftbox), static_cast<uint8_t> (topbox+1),
		static_cast<uint8_t>(itemX),static_cast<uint8_t>(bottombox-1) };
	_vfd.writePacket(buff3, sizeof(buff3), 1000);
	
}


void DisplayMgr::drawRadioScreen(modeTransition_t transition){
	
	PiCarMgr* mgr	= PiCarMgr::shared();
	RadioMgr* radio 	= PiCarMgr::shared()->radio();
	
	int centerX = _vfd.width() /2;
	int centerY = _vfd.height() /2;
	
	static bool didSetRing = false;
	
	static RadioMgr::radio_mode_t lastMode = RadioMgr::MODE_UNKNOWN;
	
	//	printf("display RadioScreen %s %s %d |%s| \n",redraw?"REDRAW":"", shouldUpdate?"UPDATE":"" ,
	//			 radio->radioMuxMode(),
	//			 	RadioMgr::muxstring(radio->radioMuxMode()).c_str() );
	
	if(transition == TRANS_LEAVING) {
		_rightRing.clearAll();
		didSetRing = false;
		return;
	}
	
	if(transition == TRANS_ENTERING) {
		_vfd.clearScreen();
		_rightRing.clearAll();
		
		if(_backlightKnobs){
			setKnobColor(KNOB_RIGHT, RGB::Lime);
			setKnobColor(KNOB_LEFT, RGB::Lime);
		}
		else {
			setKnobColor(KNOB_RIGHT, RGB::Black);
			setKnobColor(KNOB_LEFT, RGB::Black);
		}

		didSetRing = false;
	}
	
	if(transition == TRANS_IDLE) {
		_rightRing.clearAll();
		didSetRing = false;
	}
	
	// avoid doing a needless refresh.  if this was a timeout event,  then just update the time
	if(transition == TRANS_ENTERING || transition == TRANS_REFRESH){
		
		if(! radio->isOn()){
			string str = "OFF";
			auto textCenter =  centerX - (str.size() * 11);
			
			TRY(_vfd.setFont(VFD::FONT_10x14));
			TRY(_vfd.setCursor( textCenter ,centerY+5));
			TRY(_vfd.write(str));
		}
		else {
			RadioMgr::radio_mode_t  mode  = radio->radioMode();
			uint32_t 					freq =  radio->frequency();
			// we might need an extra refresh if switching modes
			if(lastMode != mode){
				_vfd.clearScreen();
				_rightRing.clearAll();
				didSetRing = false;
				lastMode = mode;
			}
			
			
			if(mode == RadioMgr::AUX){
				
				string str = "AUX";
				auto freqCenter =  centerX  -( (str.size() /2)  * 11) - 7 ;
				
				TRY(_vfd.setFont(VFD::FONT_10x14));
				TRY(_vfd.setCursor( freqCenter ,centerY+5));
				TRY(_vfd.write(str));
				
			}
			else {
				
				uint32_t 	maxFreq, minFreq;
				bool hasRange =  RadioMgr::freqRangeOfMode(mode, minFreq, maxFreq);
				
				if(hasRange){
					uint32_t newfreq = fmax(minFreq, fmin(maxFreq, freq));  //  pin freq
					uint8_t offset =   ( float(newfreq-minFreq)  / float( maxFreq-minFreq)) * 23 ;
					
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
					
					didSetRing = true;
				}
				
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
				
				auto modeStart = 5;
				if(precision == 0)
					modeStart += 15;
				else if  (precision == 1)
					modeStart += 5;
				
				TRY(_vfd.setFont((modStr.size() > 3)?VFD::FONT_MINI:VFD::FONT_5x7 ));
				TRY(_vfd.setCursor(modeStart, centerY-3));
				TRY(_vfd.write(modStr));
				
				TRY(_vfd.setFont(VFD::FONT_10x14));
				TRY(_vfd.setCursor( freqCenter ,centerY+5));
				TRY(_vfd.write(str));
				
				TRY(_vfd.setFont(VFD::FONT_5x7));
				TRY(_vfd.write( " " + hzstr));
				
				// Draw title centered inb char buffer
				constexpr int  titleMaxSize = 20;
				char titlebuff[titleMaxSize + 1];
				memset(titlebuff,' ', titleMaxSize);
				titlebuff[titleMaxSize] = '\0';
				int titleStart =  centerX - ((titleMaxSize * 6)/2);
				int titleBottom = centerY -14;
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
	
	RadioMgr::radio_mux_t 	mux  =  radio->radioMuxMode();
	string muxstring = RadioMgr::muxstring(mux);
	
	TRY(_vfd.setFont(VFD::FONT_MINI));
	TRY(_vfd.setCursor(8, centerY+5));
	TRY(_vfd.write(muxstring));
	
	drawEngineCheck();
	
	drawTimeBox();
}

void DisplayMgr::drawSettingsScreen(modeTransition_t transition){
	//printf("drawSettingsScreen %d\n",transition);
	
	
	if(transition == TRANS_ENTERING) {
		_rightKnob.setAntiBounce(antiBounceSlow);
		setKnobColor(KNOB_RIGHT, RGB::Orange);
		_vfd.clearScreen();
	}
	
	if(transition == TRANS_LEAVING) {
		_rightKnob.setAntiBounce(antiBounceDefault);
//		setKnobColor(KNOB_RIGHT, RGB::Lime);
		return;
	}
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(0,10));
	TRY(_vfd.write("Settings"));
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(_vfd.width()-5,60));
	TRY(_vfd.write(">"));
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
		setKnobColor(KNOB_RIGHT, RGB::Yellow);
		_vfd.clearScreen();
		
#if !USE_SERIAL_GPS
		gps->setShouldRead(true);
#endif
		
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
//		setKnobColor(KNOB_RIGHT, RGB::Lime);
#if !USE_SERIAL_GPS
		gps->setShouldRead(false);
#endif
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
		
		_vfd.printPacket( "%s:%2d DOP:%.1f",
							  GPSmgr::NavString(location.navSystem).c_str(), location.numSat, location.HDOP/10.);
	}
	
	
	GPSVelocity_t velocity;
	if(gps->GetVelocity(velocity)){
		
		_vfd.setFont(VFD::FONT_5x7);
		_vfd.setCursor(midX +20 ,utmRow+20);
		_vfd.printPacket("%3d\xa0",int(velocity.heading));
		
		_vfd.setCursor(midX +20 ,altRow+10);
		double mph = velocity.speed * 0.6213711922;
		_vfd.printPacket("%3d",int(mph));
		_vfd.setFont(VFD::FONT_MINI);
		_vfd.printPacket(" M/H");
	}
	
	drawTimeBox();
}


void DisplayMgr::drawShutdownScreen(){
	
	
	//	printf("shutdown display");
	_vfd.clearScreen();
	_vfd.clearScreen();
	_rightRing.clearAll();
	_leftRing.clearAll();
	
	setKnobColor(KNOB_RIGHT, RGB::Black);
	setKnobColor(KNOB_LEFT, RGB::Black);

	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(10,35));
	TRY(_vfd.write("  Well... Bye"));
	sleep(1);
}



void DisplayMgr::drawCANBusScreen(modeTransition_t transition){
	
	PiCarCAN*	can 	= PiCarMgr::shared()->can();
	
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	int64_t nowSecs = timespec_to_msec(&now) / 1000;
	
	
	constexpr int busTimeout = 5;
	
	if(transition == TRANS_ENTERING) {
		_rightKnob.setAntiBounce(antiBounceSlow);
		setKnobColor(KNOB_RIGHT, RGB::Red);
		_vfd.clearScreen();
	}
	
	if(transition == TRANS_LEAVING) {
		_rightKnob.setAntiBounce(antiBounceDefault);
//		setKnobColor(KNOB_RIGHT, RGB::Lime);
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
	TRY(_vfd.write(">"));
	
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
		setKnobColor(KNOB_RIGHT, RGB::Red);
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
//		setKnobColor(KNOB_RIGHT, RGB::Lime);
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



void DisplayMgr::drawTimeBox(){
	// Draw time
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	char timebuffer[16] = {0};
	std::strftime(timebuffer, sizeof(timebuffer)-1, "%2l:%M%P", t);
	_vfd.setFont(VFD::FONT_5x7);
	_vfd.setCursor(_vfd.width() - (strlen(timebuffer) * 6) ,7);
	_vfd.write(timebuffer);
	
}

void DisplayMgr::drawInfoScreen(modeTransition_t transition){
	
	uint8_t col = 0;
	uint8_t row = 7;
	string str;
	static uint8_t lastrow = 0;
	
	PiCarMgr*			mgr 	= PiCarMgr::shared();
	RadioMgr*			radio 	= mgr->radio();
	GPSmgr*				gps 		= mgr->gps();
	PiCarDB*				db 		= mgr->db();
	PiCarCAN*			can 		= mgr->can();
#if USE_COMPASS
	CompassSensor* 	compass	= mgr->compass();
#endif
	
	if(transition == TRANS_LEAVING) {
		lastrow = 0;
		return;
	}
	
	if(transition == TRANS_ENTERING){
		
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
	
	{
		float cTemp = 0;
		int  fanspeed = 0;
		
		if(db->getFloatValue(VAL_CPU_INFO_TEMP, cTemp)){
			
			_vfd.setCursor(col+10, 64 );
			
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
	//	printf("displayStartupScreen %s\n",redraw?"REDRAW":"");
}

// MARK: -  Balance Audio Screen


void DisplayMgr::drawBalanceScreen(modeTransition_t transition){
	
	AudioOutput* audio	= PiCarMgr::shared()->audio();
	
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
		string str = "Balance";
		_vfd.setCursor( midX - ((str.size()*5) /2 ), topbox - 5);
		_vfd.write(str);
		
		//draw box outline
		uint8_t buff1[] = {VFD_OUTLINE,leftbox,topbox,rightbox,bottombox };
		_vfd.writePacket(buff1, sizeof(buff1), 0);
		
		_vfd.setCursor(leftbox - 10, bottombox -1 );
		_vfd.write("L");
		_vfd.setCursor(rightbox + 5, bottombox -1 );
		_vfd.write("R");
	}
	
	// avoid doing a needless refresh.  if this was a timeout event,  then just update the time
	if(transition == TRANS_ENTERING || transition == TRANS_REFRESH){
		
		double balance = audio->balance();
		
		uint8_t itemX = midX +  ((rightbox - leftbox)/2) * balance;
		itemX &= 0xfE; // to nearest 2
		itemX = max(itemX,  static_cast<uint8_t> (leftbox+2) );
		itemX = min(itemX,  static_cast<uint8_t> (rightbox-6) );
		
		// clear inside of box
		uint8_t buff2[] = {VFD_CLEAR_AREA,
			static_cast<uint8_t>(leftbox+1), static_cast<uint8_t> (topbox+1),
			static_cast<uint8_t>(rightbox-1),static_cast<uint8_t>(bottombox-1),
			VFD_SET_CURSOR, midX, static_cast<uint8_t>(bottombox -1),'|',
			// draw marker
			VFD_SET_WRITEMODE, 0x03, 	// XOR
			VFD_SET_CURSOR, itemX, static_cast<uint8_t>(bottombox -1), 0x5F,
			VFD_SET_WRITEMODE, 0x00,};	// Normal
		
		_vfd.writePacket(buff2, sizeof(buff2), 0);
		
	}
}


// MARK: -  Fader Audio Screen


void DisplayMgr::drawFaderScreen(modeTransition_t transition){
	
	AudioOutput* audio	= PiCarMgr::shared()->audio();
	
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
		string str = "Fade";
		_vfd.setCursor( midX - ((str.size()*5) /2 ), topbox - 5);
		_vfd.write(str);
		
		//draw box outline
		uint8_t buff1[] = {VFD_OUTLINE,leftbox,topbox,rightbox,bottombox };
		_vfd.writePacket(buff1, sizeof(buff1), 0);
		
		_vfd.setCursor(leftbox - 10, bottombox -1 );
		_vfd.write("F");
		_vfd.setCursor(rightbox + 5, bottombox -1 );
		_vfd.write("R");
	}
	
	// avoid doing a needless refresh.  if this was a timeout event,  then just update the time
	if(transition == TRANS_ENTERING || transition == TRANS_REFRESH){
		
		double fader = audio->fader();
		
		uint8_t itemX = midX +  ((rightbox - leftbox)/2) * fader;
		itemX &= 0xfE; // to nearest 2
		itemX = max(itemX,  static_cast<uint8_t> (leftbox+2) );
		itemX = min(itemX,  static_cast<uint8_t> (rightbox-6) );
		
		// clear inside of box
		uint8_t buff2[] = {VFD_CLEAR_AREA,
			static_cast<uint8_t>(leftbox+1), static_cast<uint8_t> (topbox+1),
			static_cast<uint8_t>(rightbox-1),static_cast<uint8_t>(bottombox-1),
			VFD_SET_CURSOR, midX, static_cast<uint8_t>(bottombox -1),'|',
			// draw marker
			VFD_SET_WRITEMODE, 0x03, 	// XOR
			VFD_SET_CURSOR, itemX, static_cast<uint8_t>(bottombox -1), 0x5F,
			VFD_SET_WRITEMODE, 0x00,};	// Normal
		
		_vfd.writePacket(buff2, sizeof(buff2), 0);
		
	}
}

// MARK: -  Balance/ Fader / Brightness knob selector

bool DisplayMgr::processSelectorKnobActionForBalance( knob_action_t action){
	bool wasHandled = false;
	
	AudioOutput* audio	= PiCarMgr::shared()->audio();
	
	double balance = audio->balance();
	// limit the precision
	balance = std::floor((balance * 100) + .5) / 100;
	
	if(action == KNOB_UP){
		
		if(balance < 1.0){
			audio->setBalance(balance +.1);
			setEvent(EVT_NONE,MODE_BALANCE);
		}
		wasHandled = true;
	}
	
	else if(action == KNOB_DOWN){
		
		if(balance > -1.0){
			audio->setBalance(balance -.1);
			setEvent(EVT_NONE,MODE_BALANCE);
		}
		wasHandled = true;
	}
	else if(action == KNOB_CLICK){
		popMode();
	}
	
	return wasHandled;
}

bool DisplayMgr::processSelectorKnobActionForFader( knob_action_t action){
	bool wasHandled = false;
	
	AudioOutput* audio	= PiCarMgr::shared()->audio();
	
	double fade = audio->fader();
	// limit the precision
	fade = std::floor((fade * 100) + .5) / 100;
	
	if(action == KNOB_UP){
		
		if(fade < 1.0){
			audio->setFader(fade +.1);
			setEvent(EVT_NONE,MODE_FADER);
		}
		wasHandled = true;
	}
	
	else if(action == KNOB_DOWN){
		
		if(fade > -1.0){
			audio->setFader(fade -.1);
			setEvent(EVT_NONE,MODE_FADER);
		}
		wasHandled = true;
	}
	else if(action == KNOB_CLICK){
		popMode();
	}
	
	return wasHandled;
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
		
		uint8_t buff2[] = {VFD_CLEAR_AREA,
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
			
			_vfd.printLines(20, 6, lines, firstLine, displayedLines);
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
			// erase codes.
			can->sendDTCEraseRequest();
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

