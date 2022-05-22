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


DisplayMgr::DisplayMgr(){
	_eventQueue = {};
	
}

DisplayMgr::~DisplayMgr(){
	
}


bool DisplayMgr::begin(const char* path, speed_t speed){
	int error = 0;
	
	return begin(path, speed, error);
}

bool DisplayMgr::begin(const char* path, speed_t speed,  int &error){
	
	_isSetup = false;
	
	if(!_vfd.begin(path,speed,error))
		throw Exception("failed to setup VFD ");
	
	if( !(_rightRing.begin(0x60, error)
			&& _leftRing.begin(0x61, error))){
		throw Exception("failed to setup LEDrings ");
	}
	
	// flip the ring numbers
	_rightRing.setOffset(0,true);
	_leftRing.setOffset(2, true);		// slight offset for volume control of zero
	
	if( _vfd.reset()
		&& _rightRing.reset()
		&& _leftRing.reset()
		&& _rightRing.clearAll()
		&& _leftRing.clearAll())
		_isSetup = true;
	
	if(_isSetup) {
		
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
		
		resetMenu();
		
		pthread_attr_t attr;
		pthread_attr_init (&attr);
		
		pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS);
		
		pthread_create(&_updateTID, &attr,
							(THREADFUNCPTR) &DisplayMgr::DisplayUpdateThread, (void*)this);
		showStartup();
	}
	
	return _isSetup;
}


void DisplayMgr::stop(){
	
	if(_isSetup){
		
		if(_menuCB) _menuCB(false, 0);
		resetMenu();
		pthread_cond_signal(&_cond);
		pthread_join(_updateTID, NULL);
		
		_vfd.stop();
		_rightRing.stop();
		_leftRing.stop();
	}
	
	_isSetup = false;
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
	pthread_cond_signal(&_cond);
	pthread_mutex_unlock (&_mutex);
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
		
		if(ledStep < 24 * 10){
 			_leftRing.setColor( mod(ledStep, 23), 0, 0, 0);
			ledStep++;
			_leftRing.setColor(mod(ledStep, 23), 255, 255, 255);
 //			printf("\nLED RUN %d\n",ledStep);
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

	if( _ledEvent & LED_EVENT_VOL ){
		gettimeofday(&startedEvent, NULL);
		ledEventSet(LED_EVENT_VOL_RUNNING,LED_EVENT_VOL );
		
	 	printf("\nVOL STARTUP\n");
	}
	else if( _ledEvent & LED_EVENT_VOL_RUNNING ){
		
		timeval now, diff;
		gettimeofday(&now, NULL);
		timersub(&now, &startedEvent, &diff);

		if(diff.tv_sec <  5){
	 
			printf("\nVOL RUN\n");
	
		}
		else {
			ledEventSet(0, LED_EVENT_VOL_RUNNING);
 			printf("\nVOL RUN DONE\n");

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


// MARK: -  change modes


void DisplayMgr::showStartup(){
	setEvent(EVT_PUSH, MODE_STARTUP );
}


void DisplayMgr::showTime(){
	setEvent(EVT_PUSH, MODE_TIME);
}

void DisplayMgr::showDiag(){
	setEvent(EVT_PUSH, MODE_DIAG);
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
	
	if(shouldPush){
		_eventQueue.push({evt,mod});
		pthread_cond_signal(&_cond);
	}
	pthread_mutex_unlock (&_mutex);
}

// MARK: -  Menu Mode

void DisplayMgr::resetMenu() {
	_menuItems.clear();
	_currentMenuItem = 0;
	_menuTimeout = 0;
	_menuCB = nullptr;
	
}

void DisplayMgr::showMenuScreen(vector<menuItem_t> items, uint intitialItem, time_t timeout,
										  menuSelectedCallBack_t cb){
	
	resetMenu();
	_menuItems = items;
	_currentMenuItem =  min( max( static_cast<int>(intitialItem), 0),static_cast<int>( _menuItems.size()) -1);
	_menuCursor	= 0;
	
	_menuTimeout = timeout;
	_menuCB = cb;
	
	setEvent(EVT_PUSH,MODE_MENU);
}

void DisplayMgr::menuSelectAction(menu_action action){
	
	if(isMenuDisplayed()) {
		
		switch(action){
				
			case MENU_EXIT:
				if(_menuCB) {
					_menuCB(false, 0);
				}
				setEvent(EVT_POP, MODE_UNKNOWN);
				resetMenu();
				break;
				
			case MENU_UP:
				_currentMenuItem = min(_currentMenuItem + 1,  static_cast<int>( _menuItems.size() -1));
				setEvent(EVT_NONE,MODE_MENU);
				break;
				
			case MENU_DOWN:
				_currentMenuItem = max( _currentMenuItem - 1,  static_cast<int>(0));
				setEvent(EVT_NONE,MODE_MENU);
				break;
				
			case MENU_CLICK:
				if(_menuCB) {
					_menuCB(true,  _currentMenuItem);
				}
				setEvent(EVT_POP, MODE_UNKNOWN);
				resetMenu();
				break;
		}
		
	}
}


void DisplayMgr::drawMenuScreen(modeTransition_t transition){
	
	
	//	uint8_t width = _vfd.width();
	uint8_t height = _vfd.height();
	
	uint8_t startV =  25;
	uint8_t lineHeight = 9;
	uint8_t maxLines =  (height - startV) / lineHeight ;
	//	uint8_t maxCol = width / 7;
	
	if(transition == TRANS_LEAVING) {
		return;
	}
	
	if(transition == TRANS_ENTERING) {
		_vfd.clearScreen();
		TRY(_vfd.setFont(VFD::FONT_5x7));
		TRY(_vfd.setCursor(20,10));
		TRY(_vfd.write("Select Screen"));
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
		for(int i = _menuCursor; i <= _menuCursor + maxLines; i ++){
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
		case MODE_DIAG:
		case MODE_GPS:
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



// MARK: -  DisplayUpdate thread

void DisplayMgr::DisplayUpdate(){
	
	bool shouldQuit = false;
 	//	printf("start DisplayUpdate\n");
	
	while(!shouldQuit){
		
		// --check if any events need processing else wait for a timeout
		struct timespec ts = {0, 0};
		clock_gettime(CLOCK_REALTIME, &ts);
		
		pthread_mutex_lock (&_mutex);
		// if there are LED events, run the update every half second
		// elese wait a whole second
		if(_ledEvent){
			ts.tv_sec += 0;
			ts.tv_nsec += 5.0e8;		// half second
		}
		else {
			ts.tv_sec += 1;
			ts.tv_nsec += 0;
		}
		
		if ((_eventQueue.size() == 0)
			 && ((_ledEvent & 0x0000ffff) == 0))		// new LED events..
			pthread_cond_timedwait(&_cond, &_mutex, &ts);
		
		eventQueueItem_t item = {EVT_NONE,MODE_UNKNOWN};
		if(_eventQueue.size()){
			item = _eventQueue.front();
			_eventQueue.pop();
		}
		
		mode_state_t lastMode = _current_mode;
		pthread_mutex_unlock (&_mutex);
		
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
					if(diff.tv_sec >=  3) {
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
				
				if(item.mode == MODE_SHUTDOWN){
					shouldQuit = true;		// bail now
					continue;
				}
				else if(pushMode(item.mode)){
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
				
			case MODE_DIAG:
				drawDiagScreen(transition);
				break;
				
			case MODE_MENU:
				drawMenuScreen(transition);
				break;
				
			case MODE_GPS:
				drawGPSScreen(transition);
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
		
		// scan the LEDS off
		for (int i = 0; i < 24; i++) {
			_leftRing.setColor( i, 0, 0, 0);
			usleep(10 * 1000);
		}
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
		
		// volume LED scales between 1 and 24
		int ledvol = volume*23;
		for (int i = 0 ; i < 24; i++) {
			_leftRing.setGREEN(i, i <= ledvol?0xff:0 );
		}
		
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

void DisplayMgr::drawDiagScreen(modeTransition_t transition){
//	printf("displayDiagScreen %d\n",transition);
	
	
	if(transition == TRANS_ENTERING) {
		_vfd.clearScreen();
	}

	if(transition == TRANS_LEAVING) {
		return;
	}
	
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.setCursor(0,10));
   TRY(_vfd.write("Diagnostics"));
	
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
		_vfd.clearScreen();
	}

	if(transition == TRANS_LEAVING) {
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
#define M2FT 	3.2808399
			sprintf(buffer, "%.1f ft",location.altitude * M2FT);
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
