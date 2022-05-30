//
//  DisplayMgr.cpp
//  vfdtest
//
//  Created by Vincent Moscaritolo on 4/25/22.
//

#include "DisplayMgr.hpp"
#include <string>
#include <iomanip>
#include <time.h>
#include <sys/time.h>
#include <iostream>
#include <stdlib.h>
#include <cmath>
#include <algorithm>
#include "Utils.hpp"

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



DisplayMgr::DisplayMgr(){
	_eventQueue = {};
 	_ledEvent = 0;
	_isSetup = false;
	_isRunning = true;
	
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
	_rightRing.setOffset(0,true);
	_leftRing.setOffset(0, true);		// slight offset for volume control of zero
	
	if( _vfd.reset()
		&& _rightRing.reset()
		&& _leftRing.reset()
		&& _rightRing.clearAll()
		&& _leftRing.clearAll())
		_isSetup = true;
	
	if(_isSetup) {
		
		_rightKnob.setAntiBounce(antiBounceDefault);
		_leftKnob.setAntiBounce(antiBounceDefault);
		
		setKnobColor(KNOB_RIGHT, RGB::Lime);
		setKnobColor(KNOB_LEFT, RGB::Lime);

		// Set for normal operation
		_rightRing.setConfig(0x01);
		_leftRing.setConfig(0x01);
		
		// full scaling -- control current with global curent
		_rightRing.SetScaling(0xFF);
		_leftRing.SetScaling(0xFF);
		
		// dont fool with this.
		_rightRing.GlobalCurrent(010);
		_leftRing.GlobalCurrent(010);
		
		// clear all values
		_rightRing.clearAll();
		_rightRing.clearAll();
		
		_eventQueue = {};
		_ledEvent = 0;
		
		resetMenu();
		showStartup();
	}
	
	return _isSetup;
}


void DisplayMgr::stop(){
	
	if(_isSetup){
		
		if(_menuCB) _menuCB(false, 0);
		resetMenu();
		_eventQueue = {};
		_ledEvent = 0;


		// shut down the display loop
		_isSetup = false;
		pthread_cond_signal(&_cond);

		usleep(500);
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
	ledEventSet(LED_EVENT_STARTUP, 0);
}

void DisplayMgr::LEDeventVol(){
	ledEventSet(LED_EVENT_VOL,0);
}
 
 
void DisplayMgr::ledEventUpdate(){
	
	if( _ledEvent & (LED_EVENT_STARTUP | LED_EVENT_STARTUP_RUNNING))
		runLEDEventStartup();

	if( _ledEvent & (LED_EVENT_VOL | LED_EVENT_VOL_RUNNING))
		runLEDEventVol();

	
}
 

void DisplayMgr::ledEventSet(uint32_t set, uint32_t reset){
	pthread_mutex_lock (&_mutex);
	_ledEvent |= set;
	_ledEvent &= ~reset;
	pthread_mutex_unlock (&_mutex);
	pthread_cond_signal(&_cond);
	}

void DisplayMgr::runLEDEventStartup(){
	
 	static uint8_t 	ledStep = 0;
	
	if( _ledEvent & LED_EVENT_STARTUP ){
 	ledEventSet(LED_EVENT_STARTUP_RUNNING,LED_EVENT_STARTUP );
		
		ledStep = 0;
//		printf("\nLED STARTUP\n");
		_leftRing.clearAll();
	}
	else if( _ledEvent & LED_EVENT_STARTUP_RUNNING ){
		
		if(ledStep < 24 * 4){
			
#if 1
			DuppaLEDRing::led_block_t data = {{0,0,0}};
			data[mod(++ledStep, 24)] = {255,255,255};
			_leftRing.setLEDs(data);
			
#else
			_leftRing.setColor( mod(ledStep, 24), 0, 0, 0);
			ledStep++;
			_leftRing.setColor(mod(ledStep, 24), 255, 255, 255);
#endif
			
		}
		else {
			ledEventSet(0, LED_EVENT_STARTUP_RUNNING);
			_leftRing.clearAll();
			
//			printf("\nLED RUN DONE\n");
 
		}
  	}
 }

void DisplayMgr::runLEDEventVol(){
	
	static timeval		startedEvent = {0,0};
	AudioOutput*		audio 	= PiCarMgr::shared()->audio();

	if( _ledEvent & LED_EVENT_VOL ){
		gettimeofday(&startedEvent, NULL);
		ledEventSet(LED_EVENT_VOL_RUNNING,LED_EVENT_VOL );
		
//	 	printf("\nVOL STARTUP\n");
	}
	else if( _ledEvent & LED_EVENT_VOL_RUNNING ){
		
		timeval now, diff;
		gettimeofday(&now, NULL);
		timersub(&now, &startedEvent, &diff);

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

bool DisplayMgr::setBrightness(uint8_t level) {
	
	bool success = false;
	if(_isSetup){
		success = _vfd.setBrightness(level);
	}
	
	return success;
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
 
void DisplayMgr::showSettings(uint8_t page){
	
	switch (page) {
		case 0:
			setEvent(EVT_PUSH, MODE_SETTINGS);
			break;
			
		case 1:
			setEvent(EVT_PUSH, MODE_SETTINGS1);
			break;
			
		default:
			setEvent(EVT_PUSH, MODE_SETTINGS);
	}
	
}



void DisplayMgr::showVolumeChange(){
 	setEvent(EVT_PUSH, MODE_VOLUME );
}


void DisplayMgr::showBalanceChange(){
	setEvent(EVT_PUSH, MODE_BALANCE );
}

void DisplayMgr::showRadioChange(){
	setEvent(EVT_PUSH, MODE_RADIO );
	
}
void DisplayMgr::showGPS(){
	setEvent(EVT_PUSH, MODE_GPS);
}

void DisplayMgr::showCANbus(uint8_t page){
	
	switch (page) {
		case 0:
			setEvent(EVT_PUSH, MODE_CANBUS);
			break;
			
		case 1:
			setEvent(EVT_PUSH, MODE_CANBUS1);
			break;
			
		default:
			setEvent(EVT_PUSH, MODE_CANBUS);
	}
	
}


void DisplayMgr::setEvent(event_t evt, mode_state_t mod){
	
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
		_eventQueue.push({evt,mod});
 
	pthread_mutex_unlock (&_mutex);
	
	if(shouldPush)
 		pthread_cond_signal(&_cond);
}

// MARK: -  Knob Management
 
bool  DisplayMgr::isScreenDisplayedMultiPage(){
	switch (_current_mode) {
		case MODE_CANBUS:
		case MODE_CANBUS1:
			
		case MODE_SETTINGS:
		case MODE_SETTINGS1:

		case MODE_MENU:
			return true;
	 
		default:
			return false;
	}
	
}


bool DisplayMgr::selectorKnobAction(knob_action_t action){
	
	bool wasHandled = false;
	
//	printf("selectorKnobAction (%d)\n", action);
	
	typedef   map <knob_action_t,  mode_state_t> next_state_t;
	static map <mode_state_t,  next_state_t> next_mode_map  = {
		{ MODE_CANBUS,  { {KNOB_UP , 	 MODE_CANBUS1},  {KNOB_DOWN , MODE_NOCHANGE} } },
		{ MODE_CANBUS1, { {KNOB_DOWN ,  MODE_CANBUS},  {KNOB_UP ,  MODE_NOCHANGE}  } },
 
	};
	
	if(isScreenDisplayedMultiPage()){
		if(_current_mode == MODE_MENU){
			wasHandled =  menuSelectAction(action);
		}
		else {
			
			mode_state_t nextMode = MODE_UNKNOWN;
			
			if(next_mode_map.count(_current_mode)
				&& next_mode_map[_current_mode].count(action))
				nextMode = next_mode_map[_current_mode][action];
			
			if(nextMode == MODE_NOCHANGE){		// ignore event
				wasHandled = true;
			}
			else if(nextMode != MODE_UNKNOWN) {
				setEvent(EVT_PUSH, nextMode );
				wasHandled = true;
			}
		}
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

bool DisplayMgr::menuSelectAction(knob_action_t action){
	bool wasHandled = false;
	
	if(_current_mode == MODE_MENU) {
		wasHandled = true;
 
		switch(action){
				
			case KNOB_EXIT:
				if(_menuCB) {
					_menuCB(false, 0);
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
				
			case KNOB_CLICK:
				
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
				popMode();
				
				popMode();	// do it twice.. remove the old mode.
				pthread_mutex_unlock (&_mutex);
	 
	 
				if(cb) {
					cb(true,  item);
				}
				
				
			//		_display.redraw();


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
		setKnobColor(KNOB_RIGHT, RGB::Lime);
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


bool DisplayMgr::isScreenDisplayed(mode_state_t mode, uint8_t &page){
	
	// for main page
	if( mode == _current_mode){
		page = 0;
		return true;
	}
	// handle multi page
	else switch(mode) {
		case MODE_CANBUS:
			
			if(_current_mode == MODE_CANBUS1){
				page = 1;
				return true;
			}
			break;

		case MODE_SETTINGS:
			
			if(_current_mode == MODE_SETTINGS1){
				page = 1;
				return true;
			}
			break;

		default: break;
	}
	
	return false;
	
}

bool DisplayMgr::isStickyMode(mode_state_t md){
	bool isSticky = false;
	
	switch(md){
		case MODE_TIME:
		case MODE_RADIO:
		case MODE_SETTINGS:
		case MODE_SETTINGS1:
		case MODE_GPS:
		case MODE_CANBUS:
		case MODE_CANBUS1:
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
		if(isStickyMode(_current_mode))
			_saved_mode = _current_mode;
		_current_mode = newMode;
		didChange = true;
		
	}
	return didChange;
}

void  DisplayMgr::popMode(){
	_current_mode = _saved_mode==MODE_UNKNOWN ? MODE_TIME:_saved_mode;
	_saved_mode = MODE_UNKNOWN;
	
}

void DisplayMgr::redraw(){
	
	setEvent(EVT_REDRAW, _current_mode );
}



// MARK: -  DisplayUpdate thread

void DisplayMgr::DisplayUpdate(){
	
  	//	printf("start DisplayUpdate\n");
	
	while(_isRunning){
		
		// if not setup // check back later
		if(!_isSetup){
			usleep(1000);
			continue;
		}

		// --check if any events need processing else wait for a timeout
		struct timespec ts = {0, 0};
		clock_gettime(CLOCK_REALTIME, &ts);
		
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
		
		switch(item.evt){
			
		 	// timeout - nothing happened
			case EVT_NONE:
				timeval now, diff;
				gettimeofday(&now, NULL);
				timersub(&now, &_lastEventTime, &diff);
				
				// check for startup timeout delay
				if(_current_mode == MODE_STARTUP) {
					if(diff.tv_sec >=  1) {
						pushMode(MODE_TIME);
						shouldRedraw = true;
						shouldUpdate = true;
					}
				}
				else if(_current_mode == MODE_MENU) {
					
					// check for {EVT_NONE,MODE_MENU}  which is a menu change
					if(item.mode == MODE_MENU) {
						gettimeofday(&_lastEventTime, NULL);
						shouldRedraw = false;
						shouldUpdate = true;
					}
					
					// check for menu timeout delay
					else if(_menuTimeout > 0 && diff.tv_sec >= _menuTimeout){
						if(!isStickyMode(_current_mode)){
							popMode();
							
							if(_menuCB) {
								_menuCB(false, 0);
							}
							resetMenu();
							shouldRedraw = true;
							shouldUpdate = true;
						}
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
				}
				gettimeofday(&_lastEventTime, NULL);
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
				gettimeofday(&_lastEventTime, NULL);
				shouldRedraw = true;
//				shouldUpdate = true;
				break;
		}
		
		if(lastMode != _current_mode)
			drawMode(TRANS_LEAVING, lastMode );
		
		if(shouldRedraw)
			drawMode(TRANS_ENTERING, _current_mode );
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


void DisplayMgr::drawMode(modeTransition_t transition, mode_state_t mode){
	
	if(!_isSetup)
		return;
	
	
	vector<string> l1 = { "ENT","RFR","IDL","XIT"};
	
	if(transition != TRANS_IDLE)
		printf("drawMode %s %d\n", l1[transition].c_str(),  mode);
	
//	if(transition != TRANS_IDLE){
//		printf("drawMode trans:%d mode %d\n", transition, mode);
//		fflush(stdout);
//	}
	
	try {
		switch (mode) {
				
			case MODE_STARTUP:
				drawStartupScreen(transition);
				break;
				
			case MODE_TIME:
				drawTimeScreen(transition);
				break;
				
			case MODE_VOLUME:
				drawVolumeScreen(transition);
				break;
				
			case MODE_BALANCE:
				drawBalanceScreen(transition);
				break;
				
			case MODE_RADIO:
				drawRadioScreen(transition);
				break;
				
			case MODE_SETTINGS:
				drawSettingsScreen(transition);
				break;
	
			case MODE_SETTINGS1:
				drawSettingsScreen1(transition);
				break;
	
			case MODE_MENU:
				drawMenuScreen(transition);
				break;
				
			case MODE_GPS:
				drawGPSScreen(transition);
				break;
	
			case MODE_CANBUS:
				drawCANBusScreen(transition);
				break;
				
			case MODE_CANBUS1:
				drawCANBusScreen1(transition);
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


static constexpr uint8_t VFD_OUTLINE = 0x14;
static constexpr uint8_t VFD_CLEAR_AREA = 0x12;
static constexpr uint8_t VFD_SET_AREA = 0x11;
static constexpr uint8_t VFD_SET_CURSOR = 0x10;
static constexpr uint8_t VFD_SET_WRITEMODE = 0x1A;


void DisplayMgr::drawStartupScreen(modeTransition_t transition){
	
	RadioMgr*	radio 	= PiCarMgr::shared()->radio();
	
	RtlSdr::device_info_t info;
	
	if(transition == TRANS_ENTERING){
		
		_vfd.setPowerOn(true);
		_vfd.clearScreen();
		_vfd.clearScreen();
		TRY(_vfd.setCursor(0,10));
		TRY(_vfd.setFont(VFD::FONT_5x7));
		TRY(_vfd.write("Starting Up..."));
		
		if(radio->getDeviceInfo(info)){
			TRY(_vfd.setCursor(0,18));
			TRY(_vfd.write(info.name));
		}

		LEDeventStartup();
	}
	
	
	//	printf("displayStartupScreen %s\n",redraw?"REDRAW":"");
}

void DisplayMgr::drawTimeScreen(modeTransition_t transition){
	
	PiCarDB*	db 	= PiCarMgr::shared()->db();
	
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	char buffer[128] = {0};
	
	if(transition == TRANS_LEAVING) {
		return;
	}
	
	if(transition == TRANS_ENTERING){
		_vfd.clearScreen();
//		_leftRing.clearAll();
	}
	
	std::strftime(buffer, sizeof(buffer)-1, "%2l:%M:%S", t);
	
	TRY(_vfd.setCursor(10,35));
	TRY(_vfd.setFont(VFD::FONT_10x14));
	TRY(_vfd.write(buffer));
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.write( (t->tm_hour > 12)?"PM":"AM"));
	
	float cTemp = 0;
	if(db->getFloatValue(VAL_OUTSIDE_TEMP, cTemp)){				// GET THIS FROM SOMEWHERE!!!
		
		float fTemp = cTemp *9.0/5.0 + 32.0;
		char buffer[64] = {0};
		
		TRY(_vfd.setCursor(10, 55));
		TRY(_vfd.setFont(VFD::FONT_5x7));
		sprintf(buffer, "%3d\xa0" "F", (int) round(fTemp) );
		TRY(_vfd.write(buffer));
	}
	
	if(db->getFloatValue(VAL_CPU_INFO_TEMP, cTemp)){
		char buffer[64] = {0};
		
		TRY(_vfd.setCursor(64, 55));
		TRY(_vfd.setFont(VFD::FONT_5x7));
		sprintf(buffer, "CPU:%d\xa0" "C ", (int) round(cTemp) );
		TRY(_vfd.write(buffer));
	}
	
}


void DisplayMgr::drawVolumeScreen(modeTransition_t transition){
	
	PiCarDB*	db 	= PiCarMgr::shared()->db();
	
	uint8_t width = _vfd.width();
	uint8_t height = _vfd.height();
	uint8_t midX = width/2;
	uint8_t midY = height/2;
	
	uint8_t leftbox 	= 20;
	uint8_t rightbox 	= width - 20;
	uint8_t topbox 	= midY -5 ;
	uint8_t bottombox = midY + 5 ;
	
	if(transition == TRANS_LEAVING) {
//
//		// scan the LEDS off
//		for (int i = 0; i < 24; i++) {
//			_leftRing.setColor( i, 0, 0, 0);
//			usleep(10 * 1000);
//		}
 		return;
	}
	
	if(transition == TRANS_ENTERING) {
		_vfd.clearScreen();
		_leftRing.clearAll();
		
		// draw centered heading
		_vfd.setFont(VFD::FONT_5x7);
		string str = "Volume";
		_vfd.setCursor( midX - ((str.size()*5) /2 ), topbox - 5);
		_vfd.write(str);
		
		//draw box outline
		uint8_t buff1[] = {VFD_OUTLINE,leftbox,topbox,rightbox,bottombox };
		_vfd.writePacket(buff1, sizeof(buff1), 0);
	}
	
	float volume = 0;
		
	if(db->getFloatValue(VAL_AUDIO_VOLUME, volume)){
		
		uint8_t itemX = leftbox +  (rightbox - leftbox) * volume;
//
//		// volume LED scales between 1 and 24
//		int ledvol = volume*23;
//		for (int i = 0 ; i < 24; i++) {
//			_leftRing.setGREEN(i, i <= ledvol?0xff:0 );
//		}
		
		//	printf("vol: %.2f X:%d L:%d R:%d\n", volume, itemX, leftbox, rightbox);
		
		// clear rest of inside of box
		if(volume < 1){
			uint8_t buff2[] = {VFD_CLEAR_AREA,
				static_cast<uint8_t>(itemX+1),  static_cast<uint8_t> (topbox+1),
				static_cast<uint8_t> (rightbox-1),static_cast<uint8_t> (bottombox-1)};
			_vfd.writePacket(buff2, sizeof(buff2), 1000);
		}
		
		// fill volume area box
		uint8_t buff3[] = {VFD_SET_AREA,
			static_cast<uint8_t>(leftbox), static_cast<uint8_t> (topbox+1),
			static_cast<uint8_t>(itemX),static_cast<uint8_t>(bottombox-1) };
		_vfd.writePacket(buff3, sizeof(buff3), 1000);
		
	}
}


void DisplayMgr::drawBalanceScreen(modeTransition_t transition){
	
	PiCarDB*	db 	= PiCarMgr::shared()->db();
	
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
	
	float balance = 0;
	
	if(db->getFloatValue(VAL_AUDIO_BALANCE, balance)){
		
		uint8_t itemX = midX +  ((rightbox - leftbox)/2) * balance;
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
		
		TRY(_vfd.setCursor(10, 55));
		TRY(_vfd.setFont(VFD::FONT_5x7));
		char buffer[16] = {0};
		sprintf(buffer, "Balance: %.2f  ", balance);
		TRY(_vfd.write(buffer));
		
	}
}


void DisplayMgr::drawRadioScreen(modeTransition_t transition){
	
	PiCarMgr* mgr	= PiCarMgr::shared();
	RadioMgr* radio 	= PiCarMgr::shared()->radio();
	
	int centerX = _vfd.width() /2;
	int centerY = _vfd.height() /2;
	
	static bool didSetRing = false;
	
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
 	}
	
	
	RadioMgr::radio_mux_t 	mux  =  radio->radioMuxMode();
	string muxstring = RadioMgr::muxstring(mux);
	
	TRY(_vfd.setFont(VFD::FONT_MINI));
	TRY(_vfd.setCursor(8, centerY+5));
	TRY(_vfd.write(muxstring));

	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	char buffer[16] = {0};
	std::strftime(buffer, sizeof(buffer)-1, "%2l:%M%P", t);
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(_vfd.width() - (strlen(buffer) * 6) ,7));
	TRY(_vfd.write(buffer));
	
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
		setKnobColor(KNOB_RIGHT, RGB::Lime);
		return;
	}
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(0,10));
	TRY(_vfd.write("Settings"));

	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(_vfd.width()-5,60));
	TRY(_vfd.write(">"));

}

void DisplayMgr::drawSettingsScreen1(modeTransition_t transition){
//	printf("displayDiagScreen %d\n",transition);
	
	if(transition == TRANS_ENTERING) {
		_rightKnob.setAntiBounce(antiBounceSlow);
		setKnobColor(KNOB_RIGHT, RGB::Orange);
		_vfd.clearScreen();
	}

	if(transition == TRANS_LEAVING) {
		_rightKnob.setAntiBounce(antiBounceDefault);
		setKnobColor(KNOB_RIGHT, RGB::Lime);
		return;
	}
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(0,10));
	TRY(_vfd.write("Settings(1)"));
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(0,60));
	TRY(_vfd.write("<"));


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
	GPSmgr*	gps 	= PiCarMgr::shared()->gps();

//	printf("GPS  %d\n",transition);
	
	if(transition == TRANS_ENTERING) {
		setKnobColor(KNOB_RIGHT, RGB::Yellow);
		_vfd.clearScreen();
	}

	if(transition == TRANS_LEAVING) {
		setKnobColor(KNOB_RIGHT, RGB::Lime);
		return;
	}
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(0,10));
	TRY(_vfd.write("GPS"));
	
	GPSLocation_t location;
	if(gps->GetLocation(location)){
		string utm = GPSmgr::UTMString(location);
		vector<string> v = split<string>(utm, " ");
		
		char buffer[64] = {0};
		
		sprintf(buffer, "%-3s",v[0].c_str());
		TRY(_vfd.setCursor(20,25));
		TRY(_vfd.write(buffer));
		
		TRY(_vfd.setCursor(40,25));
		TRY(_vfd.write(v[1]));
		
		TRY(_vfd.setCursor(40,35));
		TRY(_vfd.write(v[2]));
		
 		if(location.altitudeIsValid)  {
			constexpr double  M2FT = 	3.2808399;  
			sprintf(buffer, "%.1f",location.altitude * M2FT);
			TRY(_vfd.setCursor(20,48));
			TRY(_vfd.write(buffer));
		}
		
		sprintf(buffer, "%s:%2d DOP:%.1f",
				  GPSmgr::NavString(location.navSystem).c_str(), location.numSat, location.HDOP/10.);
		TRY(_vfd.setFont(VFD::FONT_MINI));
		TRY(_vfd.setCursor(0,60));
		TRY(_vfd.write(buffer));
 	}
	else {
//		TRY(_vfd.setCursor(20,22));
//		TRY(_vfd.write("-- No Data --"));

	}
	
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	char buffer[16] = {0};
	std::strftime(buffer, sizeof(buffer)-1, "%2l:%M%P", t);
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(_vfd.width() - (strlen(buffer) * 6) ,7));
	TRY(_vfd.write(buffer));

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
}



void DisplayMgr::drawCANBusScreen(modeTransition_t transition){
 
	PiCarCAN*	can 	= PiCarMgr::shared()->can();
	time_t now = time(NULL);
	
	constexpr int busTimeout = 5;
	
	if(transition == TRANS_ENTERING) {
		_rightKnob.setAntiBounce(antiBounceSlow);
		setKnobColor(KNOB_RIGHT, RGB::Red);
		_vfd.clearScreen();
	}

	if(transition == TRANS_LEAVING) {
		_rightKnob.setAntiBounce(antiBounceDefault);
		setKnobColor(KNOB_RIGHT, RGB::Lime);
		return;
	}
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(0,10));
	TRY(_vfd.write("CANbus"));
	
	time_t lastTime = 0;
	size_t count = 0;
	
	char buffer[64] = {0};
	char* p = buffer;
	
	// GM BUS
	if(can->lastFrameTime(PiCarCAN::CAN_GM, lastTime)){
		time_t diff = now - lastTime;
 
		if(diff < busTimeout ){
			if(can->packetCount(PiCarCAN::CAN_GM, count)){
				
			}
		}
		else count = 0;
 	}
	 
	p = buffer;
	p  += sprintf(p, "%4s: ", "GM");
	if(count > 0)
		p  += sprintf(p, "%zu", count);
	else
		p  += sprintf(p, "%-10s"," ---");

		TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(10,25));
	TRY(_vfd.write(buffer));
 
	// JEEP BUS
	if(can->lastFrameTime(PiCarCAN::CAN_JEEP, lastTime)){
		time_t diff = now - lastTime;
 
		if(diff < busTimeout ){
			if(can->packetCount(PiCarCAN::CAN_JEEP, count)){
				
			}
		}
		else count = 0;
	}
	
	p = buffer;
	p  += sprintf(p, "%4s: ", "Jeep");
	if(count > 0)
		p  += sprintf(p, "%zu", count);
	else
		p  += sprintf(p, "%-10s"," ---");

	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(10,35));
	TRY(_vfd.write(buffer));


 	struct tm *t = localtime(&now);
	char timebuffer[16] = {0};
	std::strftime(timebuffer, sizeof(timebuffer)-1, "%2l:%M%P", t);
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(_vfd.width() - (strlen(timebuffer) * 6) ,7));
	TRY(_vfd.write(timebuffer));
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(_vfd.width()-5,60));
	TRY(_vfd.write(">"));

 }


void DisplayMgr::drawCANBusScreen1(modeTransition_t transition){

	static map <uint8_t, PiCarDB::canbusdisplay_prop_t> cachedProps;

 //	PiCarCAN*	can 	= PiCarMgr::shared()->can();
	PiCarDB*	db 	= PiCarMgr::shared()->db();
	uint8_t width = _vfd.width();
	uint8_t midX = width/2;
	
	uint8_t col1 = 5;
	uint8_t col2 = midX + 5;
	uint8_t row1 = 16;
	uint8_t rowsize = 19;
	
	
	if(transition == TRANS_ENTERING) {
		
		 db->getCanbusDisplayProps(cachedProps);
		_rightKnob.setAntiBounce(antiBounceSlow);
		setKnobColor(KNOB_RIGHT, RGB::Red);
		_vfd.clearScreen();
	
		// draw titles
		_vfd.setFont(VFD::FONT_MINI);
		
		for(uint8_t	 i = 0; i < 6; i++)
			if(cachedProps.count(i+1)){
				auto item = cachedProps[i+1];
				
				if(i < 3){
					_vfd.setCursor(col1, row1 + (i  * rowsize ));
					_vfd.write(item.title);
					
				}
				else {
					_vfd.setCursor(col2, row1 + ( (i-3)  * rowsize ));
					_vfd.write(item.title);
				}
			}
	}
		
		if(transition == TRANS_LEAVING) {
			
			cachedProps.clear();
			_rightKnob.setAntiBounce(antiBounceDefault);
			setKnobColor(KNOB_RIGHT, RGB::Lime);
			return;
		}

		
	// Draw values
	_vfd.setFont(VFD::FONT_5x7);
	for(uint8_t	 i = 0; i < 3; i++){
		
		char buffer[30];
		memset(buffer, ' ', sizeof(buffer));
		
		string val1 = "";
		string val2 = "";
 
		if(cachedProps.count(i+1))
			normalizeCANvalue(cachedProps[i+1].key, val1);

		if(cachedProps.count(i+4))
			normalizeCANvalue(cachedProps[i+4].key, val2);
 		
	// spread across 21 chars
		sprintf( buffer , "%-9s  %-9s", val1.c_str(), val2.c_str());
		_vfd.setCursor(col1, row1 + (i * rowsize) + 9);
		_vfd.writePacket( (const uint8_t*) buffer,21);
	}
 
	
	
	// Draw time
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	char timebuffer[16] = {0};
	std::strftime(timebuffer, sizeof(timebuffer)-1, "%2l:%M%P", t);
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(_vfd.width() - (strlen(timebuffer) * 6) ,7));
	TRY(_vfd.write(timebuffer));
 
}
		  


// MARK: -  isplay value formatting

bool DisplayMgr::normalizeCANvalue(string key, string & value){
	
	FrameDB*	fDB 	= PiCarMgr::shared()->can()->frameDB();
	
	if(key == "GM_COOLANT_TEMP")
			value = "210\xA0";
	else 	if(key == "GM_TRANS_TEMP")
		value = "188\xA0";
	else 	if(key == "GM_OIL_PRESSURE")
		value = "48";
	else 	if(key == "LONG_FUEL_TRIM_1")
		value = "10.0";
	else 	if(key == "LONG_FUEL_TRIM_2")
		value = "7.2";
	else 	if(key == "RUN_TIME")
		value = "7:43";
 else
	 value = "---";
	
	return true;
	
}

