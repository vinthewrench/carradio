//
//  DisplayMgr.hpp
//  vfdtest
//
//  Created by Vincent Moscaritolo on 4/25/22.
//

#pragma once

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <mutex>
#include <bitset>
#include <sys/time.h>

#include "VFD.hpp"
#include "ErrorMgr.hpp"
#include "CommonDefs.hpp"

using namespace std;
 

constexpr string_view DS_KEY_OUTSIDE_TEMP	= "temp1";
constexpr string_view DS_KEY_CPU_TEMP		= "tempcpu";

constexpr string_view DS_KEY_RADIO_VOLUME		= "vol";
constexpr string_view DS_KEY_RADIO_FREQ			= "freq";
constexpr string_view DS_KEY_MODULATION_MODE	= "mode";
constexpr string_view DS_KEY_MODULATION_MUX	=	 "mux";


class DisplayDataSource {
public:

	DisplayDataSource(){}
	virtual ~DisplayDataSource() {}
 
	virtual bool getStringForKey(string_view key,  string &result) { return false;};
	virtual bool getFloatForKey(string_view key,  float &result) { return false;};
	virtual bool getIntForKey(string_view key,  int &result) { return false;};
	virtual bool getDoubleForKey(string_view key,  double &result)  { return false;};
};


class DisplayMgr {
	
	typedef enum  {
		MODE_UNKNOWN = 0,
		MODE_STARTUP,
		MODE_TIME,
		MODE_VOLUME,
		MODE_RADIO,
		MODE_DIAG,
		MODE_SHUTDOWN,		// shutdown
	}mode_state_t;

	
public:

	  static DisplayMgr *shared() {
		  if(!sharedInstance){
			  sharedInstance = new DisplayMgr;
		  }
		  return sharedInstance;
	  }

	DisplayMgr();
	~DisplayMgr();
	
	void setDataSource(DisplayDataSource * datasource) { _dataSource = datasource;};
	
	bool begin(string path, speed_t speed =  B19200);
	bool begin(string path, speed_t speed, int &error);
	void stop();

	 bool reset();
		
	bool setBrightness(uint8_t level);  // 0-7

	void showTime();
	void showDiag();

	void showStartup();
	void showVolumeChange();
	void showRadioChange();

	void drawCurrentMode(bool redraw, uint16_t event);
	
private:
  
	void drawStartupScreen(bool redraw, uint16_t event);
	void drawTimeScreen(bool redraw, uint16_t event);
	void drawVolumeScreen(bool redraw, uint16_t event);
	void drawRadioScreen(bool redraw, uint16_t event);
	void drawDiagScreen(bool redraw, uint16_t event);
	void drawInternalError(bool redraw, uint16_t event);

	mode_state_t _current_mode = MODE_UNKNOWN;
	mode_state_t _saved_mode   = MODE_UNKNOWN;
	timeval		_lastEventTime = {0,0};
 
	mode_state_t handleRadioEvent();
	bool isStickyMode(mode_state_t);
	bool pushMode(mode_state_t);
	void popMode();
	
	void setEvent(uint16_t);
 
	void DisplayUpdate();		// C++ version of thread
	// C wrappers for DisplayUpdate;
	static void* DisplayUpdateThread(void *context);
	static void DisplayUpdateThreadCleanup(void *context);

#define DISPLAY_EVENT_STARTUP	0x0002
#define DISPLAY_EVENT_EXIT 	0x0004
#define DISPLAY_EVENT_TIME 	0x0008
#define DISPLAY_EVENT_VOLUME 	0x0010
#define DISPLAY_EVENT_RADIO 	0x0020
#define DISPLAY_EVENT_DIAG 	0x0040

	uint16_t				_event = 0;
	 
	pthread_cond_t 	_cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t 	_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_t			_updateTID;
	
	static DisplayMgr *sharedInstance;
 
	// display
	bool 					_isSetup = false;
	VFD 					_vfd;
	DisplayDataSource	*_dataSource;
	// debug stuff
	
	string modeString();

};

