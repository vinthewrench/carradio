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

#include "RadioMgr.hpp"

#define TRY(_statement_) if(!(_statement_)) { \
printf("FAIL AT line: %d\n", __LINE__ ); \
}
 
DisplayMgr *DisplayMgr::sharedInstance = NULL;

typedef void * (*THREADFUNCPTR)(void *);

 
DisplayMgr::DisplayMgr(){
	
	pthread_create(&_updateTID, NULL,
								  (THREADFUNCPTR) &DisplayMgr::DisplayUpdateThread, (void*)this);
 
	showStartup();
}

DisplayMgr::~DisplayMgr(){
	
	_event |= DISPLAY_EVENT_EXIT;
	pthread_cond_signal(&_cond);
	pthread_join(_updateTID, NULL);
}


bool DisplayMgr::begin(string path, speed_t speed){
	int error = 0;

	return begin(path, speed, error);
}

bool DisplayMgr::begin(string path, speed_t speed,  int &error){
	
	_isSetup = false;
	
	if(!_vfd.begin(path,speed,error))
		throw Exception("failed to setup VFD ");
	
	if(_vfd.reset())
		_isSetup = true;
	
	
	return _isSetup;
}


void DisplayMgr::stop(){
 
	if(_isSetup){
		_vfd.stop();
	}
	
	_isSetup = false;
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
	setEvent(DISPLAY_EVENT_STARTUP);
 }


void DisplayMgr::showTime(){
	setEvent(DISPLAY_EVENT_TIME);
 }

void DisplayMgr::showDiag(){
	setEvent(DISPLAY_EVENT_DIAG);
 }


void DisplayMgr::showVolumeChange(){
	setEvent(DISPLAY_EVENT_VOLUME);
}


void DisplayMgr::showBalanceChange(){
	setEvent(DISPLAY_EVENT_BALANCE);
}

void DisplayMgr::showRadioChange(){
	setEvent(DISPLAY_EVENT_RADIO);
 }

void DisplayMgr::setEvent(uint16_t evt){
	pthread_mutex_lock (&_mutex);
	_event |= evt;
	pthread_cond_signal(&_cond);
	pthread_mutex_unlock (&_mutex);

}

// MARK: -  mode utils
//
//string DisplayMgr::modeString(){
//
//	switch (_current_mode) {
//		case MODE_UNKNOWN: return("MODE_UNKNOWN");
//		case MODE_STARTUP: return("MODE_STARTUP");
//		case MODE_TIME: return("MODE_TIME");
//		case MODE_VOLUME: return("MODE_VOLUME");
//		case MODE_BALANCE: return("MODE_BALANCE");
//		case MODE_RADIO: return("MODE_RADIO");
//		case MODE_DIAG: return("MODE_DIAG");
//		case MODE_SHUTDOWN: return("MODE_SHUTDOWN");
//
//	}
//	return "";
//}
//

bool DisplayMgr::isStickyMode(mode_state_t md){
	bool isSticky = false;
	
	switch(md){
		case MODE_TIME:
		case MODE_RADIO:
		case MODE_DIAG:
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
	
	constexpr time_t sleepTime = 1;
	
//	printf("start DisplayUpdate\n");
	
	while(!shouldQuit){
		
		bool shouldRedraw = false;
		uint16_t lastEvent;
		
		// --check if any events need processing else wait for a timeout
		struct timespec ts = {0, 0};
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += sleepTime;
		
		pthread_mutex_lock (&_mutex);
		if (_event == 0)
			pthread_cond_timedwait(&_cond, &_mutex, &ts);
		
		// daved the event
		lastEvent = _event;
		
		// get a new mode for the event. and reset that event bit
		mode_state_t newMode = MODE_UNKNOWN;
		
		if((_event & DISPLAY_EVENT_STARTUP ) != 0){
			newMode = MODE_STARTUP;
			_event &= ~DISPLAY_EVENT_STARTUP;
		}
		else if((_event & DISPLAY_EVENT_VOLUME ) != 0){
			newMode = MODE_VOLUME;
			_event &= ~DISPLAY_EVENT_VOLUME;
		}
		else if((_event & DISPLAY_EVENT_BALANCE ) != 0){
				newMode = MODE_BALANCE;
				_event &= ~DISPLAY_EVENT_BALANCE;
		}
		else if((_event & DISPLAY_EVENT_RADIO ) != 0){
			newMode = handleRadioEvent();
			_event &= ~DISPLAY_EVENT_RADIO;
		}
		else if((_event & DISPLAY_EVENT_TIME ) != 0){
			newMode = MODE_TIME;
			_event &= ~DISPLAY_EVENT_TIME;
		}
		else if((_event & DISPLAY_EVENT_DIAG ) != 0){
			newMode = MODE_DIAG;
			_event &= ~DISPLAY_EVENT_DIAG;
		}
		 else if((_event & DISPLAY_EVENT_EXIT ) != 0){
			 _event &= ~DISPLAY_EVENT_EXIT;
			 shouldQuit = true;
		 }
		
		pthread_mutex_unlock (&_mutex);
 
		if(newMode != MODE_UNKNOWN){
			if(pushMode(newMode)){
				gettimeofday(&_lastEventTime, NULL);
				shouldRedraw = true;
			}
		}

		// no event is a timeout so  update the current mode
		if(newMode == MODE_UNKNOWN ){
			timeval now, diff;
			gettimeofday(&now, NULL);
			timersub(&now, &_lastEventTime, &diff);
			
			if(_current_mode == MODE_STARTUP) {
				if(diff.tv_sec >=  3) {
					pushMode(MODE_TIME);
					shouldRedraw = true;
				}
			}
			else if(diff.tv_sec >=  2){
				// should we pop the mode?
				if(!isStickyMode(_current_mode)){
					popMode();
					shouldRedraw = true;
				}
			}
		}
		
		drawCurrentMode(shouldRedraw, lastEvent);
	}
	
}

DisplayMgr::mode_state_t DisplayMgr::handleRadioEvent(){
	mode_state_t newState = MODE_UNKNOWN;
	
	int temp;
	
	if(_dataSource
		&& _dataSource->getIntForKey(DS_KEY_MODULATION_MODE, temp)
		){
			RadioMgr::radio_mode_t newMode = (RadioMgr::radio_mode_t) temp;

		if(newMode == RadioMgr::RADIO_OFF){
			newState = MODE_TIME;
		}else {
			newState = MODE_RADIO;
		}
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
	DisplayMgr* d = (DisplayMgr*)context;

	if(d->_event){
		
	}

//	printf("cleanup display\n");
}

// MARK: -  Display Draw code

void DisplayMgr::drawCurrentMode(bool redraw, uint16_t event){
	
	if(!_isSetup)
		return;
	try {
		switch (_current_mode) {
				
			case MODE_STARTUP:
				drawStartupScreen(redraw,event);
				break;
				
			case MODE_TIME:
				drawTimeScreen(redraw,event);
				break;
				
			case MODE_VOLUME:
				drawVolumeScreen(redraw,event);
				break;

			case MODE_BALANCE:
				drawBalanceScreen(redraw,event);
				break;

			case MODE_RADIO:
				drawRadioScreen(redraw,event);
				break;
				
			case MODE_DIAG:
				drawDiagScreen(redraw,event);
				
				
			default:
				drawInternalError(redraw,event);
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


void DisplayMgr::drawStartupScreen(bool redraw, uint16_t event){
	
	RadioMgr*	radio 	= RadioMgr::shared();
 
	
	RtlSdr::device_info_t info;
	
	if(redraw)
		_vfd.clearScreen();
	
	TRY(_vfd.setCursor(0,10));
	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.write("Starting Up..."));
	
	if(radio->getDeviceInfo(info)){
		TRY(_vfd.setCursor(0,18));
		TRY(_vfd.write(info.name));
	}
	
//	printf("displayStartupScreen %s\n",redraw?"REDRAW":"");
}

void DisplayMgr::drawTimeScreen(bool redraw, uint16_t event){
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	char buffer[128] = {0};
	
	if(redraw)
		_vfd.clearScreen();

	std::strftime(buffer, sizeof(buffer)-1, "%2l:%M:%S", t);
	
	TRY(_vfd.setCursor(10,35));
	TRY(_vfd.setFont(VFD::FONT_10x14));
	TRY(_vfd.write(buffer));

	TRY(_vfd.setFont(VFD::FONT_5x7));
	TRY(_vfd.write( (t->tm_hour > 12)?"PM":"AM"));
	
	float temp = 0;
	if(_dataSource
			&& _dataSource->getFloatForKey(DS_KEY_OUTSIDE_TEMP, temp)){
		
		char buffer[64] = {0};
		
			TRY(_vfd.setCursor(10, 55));
			TRY(_vfd.setFont(VFD::FONT_5x7));
			sprintf(buffer, "%3d\xA0\x46", (int) round(temp) );
			TRY(_vfd.write(buffer));
	}
		
		
}

void DisplayMgr::drawVolumeScreen(bool redraw, uint16_t event){
	
	constexpr uint8_t rightbox 	= 13;
	constexpr uint8_t leftbox 		= 112;
	constexpr uint8_t topbox 		= 34;
	constexpr uint8_t bottombox 	= 44;
	
	constexpr uint8_t VFD_OUTLINE = 0x14;
	constexpr uint8_t VFD_CLEAR_AREA = 0x12;
	constexpr uint8_t VFD_SET_AREA = 0x11;
	
	try{
		if(redraw){
			_vfd.clearScreen();
			
			// draw centered heading
			_vfd.setFont(VFD::FONT_5x7);
			string str = "Volume";
			_vfd.setCursor(( (126 - (str.size()*6)) /2 ), 29);
			_vfd.write(str);
			
			//draw box outline
			uint8_t buff1[] = {VFD_OUTLINE,rightbox,topbox,leftbox,bottombox};
			_vfd.writePacket(buff1, sizeof(buff1), 0);
		}
		
		float vol = 0;
		if(_dataSource
			&& _dataSource->getFloatForKey(DS_KEY_AUDIO_VOLUME, vol)){
			
			uint8_t rndVol =  (int) round(vol * 100);
			uint8_t midBox =  ((uint8_t) round((leftbox - rightbox) * vol)) + rightbox - 1;
			
			// fill volume area box
			uint8_t buff3[] = {VFD_SET_AREA,rightbox,topbox+1,midBox,bottombox-1};
			_vfd.writePacket(buff3, sizeof(buff3), 0);
			
			// clear rest of inside of box
			if(rndVol < 100){
				uint8_t buff2[] = {VFD_CLEAR_AREA, static_cast<uint8_t>(midBox+1),topbox+1,leftbox-1,bottombox-1};
				_vfd.writePacket(buff2, sizeof(buff2), 0);
			}
		}
	} catch (...) {
		// ignore fail
	}
 }


void DisplayMgr::drawBalanceScreen(bool redraw, uint16_t event){
	
	uint8_t width = _vfd.width();
	uint8_t rightbox 	= 20;
	uint8_t leftbox 	= width - 20;
	uint8_t topbox 	= 34;
	uint8_t bottombox = 44;
	
	constexpr uint8_t VFD_OUTLINE = 0x14;
	constexpr uint8_t VFD_CLEAR_AREA = 0x12;
	constexpr uint8_t VFD_SET_AREA = 0x11;
	
	try{
		if(redraw){
			_vfd.clearScreen();
			
			// draw centered heading
			_vfd.setFont(VFD::FONT_5x7);
			string str = "Balance";
			_vfd.setCursor(( (126 - (str.size()*6)) /2 ), 29);
			_vfd.write(str);
			
			//draw box outline
			uint8_t buff1[] = {VFD_OUTLINE,rightbox,topbox,leftbox,bottombox};
			_vfd.writePacket(buff1, sizeof(buff1), 0);
			
			_vfd.setCursor(bottombox, rightbox - 10);
			_vfd.write("L");
			_vfd.setCursor(bottombox, leftbox + 10);
			_vfd.write("R");
 		}
		
		float balance = 0;
		if(_dataSource
			&& _dataSource->getFloatForKey(DS_KEY_AUDIO_BALANCE, balance)){
			
			TRY(_vfd.setCursor(10, 55));
			TRY(_vfd.setFont(VFD::FONT_5x7));
			
			char buffer[16] = {0};
			sprintf(buffer, "Balance: %.2f  ", balance);
			TRY(_vfd.write(buffer));
 
//			uint8_t rndVol =  (int) round(vol * 100);
//			uint8_t midBox =  ((uint8_t) round((leftbox - rightbox) * vol)) + rightbox - 1;
//
//			// fill volume area box
//			uint8_t buff3[] = {VFD_SET_AREA,rightbox,topbox+1,midBox,bottombox-1};
//			_vfd.writePacket(buff3, sizeof(buff3), 0);
//
//			// clear rest of inside of box
//			if(rndVol < 100){
//				uint8_t buff2[] = {VFD_CLEAR_AREA, static_cast<uint8_t>(midBox+1),topbox+1,leftbox-1,bottombox-1};
//				_vfd.writePacket(buff2, sizeof(buff2), 0);
//			}
		}
	} catch (...) {
		// ignore fail
	}
 }

	
void DisplayMgr::drawRadioScreen(bool redraw, uint16_t event){
//	printf("display RadioScreen %s\n",redraw?"REDRAW":"");

	try{
		if(redraw){
			_vfd.clearScreen();
		}
		
		// avoid doing a needless refresh.  if this was a timeout event,  then just update the time
		if(event != 0) {
			double freq = 0;
			int temp, temp1;
		 
			if(_dataSource
				&& _dataSource->getDoubleForKey(DS_KEY_RADIO_FREQ, freq)
				&& _dataSource->getIntForKey(DS_KEY_MODULATION_MODE, temp)
				&& _dataSource->getIntForKey(DS_KEY_MODULATION_MUX, temp1)
				){
					RadioMgr::radio_mode_t mode = (RadioMgr::radio_mode_t) temp;
					RadioMgr::radio_mux_t mux = (RadioMgr::radio_mux_t) temp1;
					
				int precision = 0;
				int centerX = _vfd.width() /2;
				int centerY = _vfd.height() /2;
				
				switch (mode) {
					case RadioMgr::BROADCAST_AM: precision = 0;break;
					case RadioMgr::BROADCAST_FM: precision = 1;break;
					default :
						precision = 3; break;
					}
				
				string str = 	RadioMgr::hertz_to_string(freq, precision);
				string hzstr =	RadioMgr::freqSuffixString(freq);
				string modStr = RadioMgr::modeString(mode);
				string muxstring = RadioMgr::muxstring(mux);
				
				auto freqCenter =  centerX - (str.size() * 11) + 18;
				if(precision > 1)  freqCenter += 10*2;

				auto modeStart = 5;
				if(precision == 0)
					modeStart += 15;
				else if  (precision == 1)
					modeStart += 5;
				 
				TRY(_vfd.setFont(VFD::FONT_5x7));
				TRY(_vfd.setCursor(modeStart, centerY-3));
				TRY(_vfd.write(modStr));
				
				TRY(_vfd.setFont(VFD::FONT_MINI));
				TRY(_vfd.setCursor(modeStart+3, centerY+5));
				TRY(_vfd.write(muxstring));
				
				TRY(_vfd.setFont(VFD::FONT_10x14));
				TRY(_vfd.setCursor( freqCenter ,centerY+5));
				TRY(_vfd.write(str));

				TRY(_vfd.setFont(VFD::FONT_5x7));
				TRY(_vfd.write( " " + hzstr));
			}
		}
	
		
			time_t now = time(NULL);
			struct tm *t = localtime(&now);
			char buffer[16] = {0};
			std::strftime(buffer, sizeof(buffer)-1, "%2l:%M%P", t);
			TRY(_vfd.setFont(VFD::FONT_5x7));
			TRY(_vfd.setCursor(_vfd.width() - (strlen(buffer) * 6) ,7));
			TRY(_vfd.write(buffer));

		
	} catch (...) {
		// ignore fail
	}
}

void DisplayMgr::drawDiagScreen(bool redraw, uint16_t event){
	printf("displayDiagScreen %s\n",redraw?"REDRAW":"");

}


void DisplayMgr::drawInternalError(bool redraw, uint16_t event){
	
	printf("displayInternalError %s\n",redraw?"REDRAW":"");
}

