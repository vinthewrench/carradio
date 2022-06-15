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
#include <utility>      // std::pair, std::make_pair
#include <string>       // std::string
#include <queue>

#include <sys/time.h>

#include "VFD.hpp"
#include "ErrorMgr.hpp"
#include "CommonDefs.hpp"
#include "DuppaLEDRing.hpp"
#include "DuppaKnob.hpp"

using namespace std;


class DisplayMgr {
	
public:

	DisplayMgr();
	~DisplayMgr();
		
	bool begin(const char* path, speed_t speed =  B19200);
	bool begin(const char* path, speed_t speed, int &error);
	void stop();

	 bool reset();
		
	// LED effects
	void LEDeventStartup();
	void LEDeventVol();
	void LEDeventMute();
	void LEDeventStop();

// active mode
	typedef enum  {
		MODE_UNKNOWN = 0,
		MODE_NOCHANGE,	  // no change in mode
		
		MODE_STARTUP,
		MODE_TIME,
		MODE_VOLUME,
		MODE_BALANCE,
		MODE_RADIO,
		MODE_CANBUS,
		MODE_GPS,
		MODE_SETTINGS,
		MODE_DTC,
		MODE_DTC_INFO,

		MODE_INFO,

		MODE_DEV_STATUS,
		MODE_MENU,
	}mode_state_t;

	mode_state_t active_mode();
 
	// knobs
	
	typedef enum  {
		KNOB_EXIT = 0,
		KNOB_UP,
		KNOB_DOWN,
		KNOB_CLICK
	}knob_action_t;
 
	typedef enum  {
		KNOB_RIGHT,
		KNOB_LEFT,
 	}knob_id_t;

	DuppaKnob* rightKnob() { return &_rightKnob;};
	DuppaKnob* leftKnob() { return &_leftKnob;};

	// display related
	bool setBrightness(uint8_t level);  // 0-7
	bool setKnobColor(knob_id_t, RGB);
	
	// multi page display
	bool usesSelectorKnob();
	bool selectorKnobAction(knob_action_t action);

	void showTime();
	void showGPS();
	void showStartup();
	void showInfo();
	void showDTC();
	void showDTCInfo(string code);
 
	void showVolumeChange();	// Deprecated
	void showBalanceChange();
	void showRadioChange();
	void showCANbus(uint8_t page = 0);
	void showSettings();

	void showDevStatus();

 	// Menu Screen Management
	typedef string menuItem_t;
	typedef std::function<void(bool didSucceed, uint selectedItemID)> menuSelectedCallBack_t;
	void showMenuScreen(vector<menuItem_t> items,
							  uint intitialItem,
							  string title,
							  time_t timeout = 0,
							  menuSelectedCallBack_t cb = nullptr);
 
private:
	
	typedef enum  {
		EVT_NONE = 0,
		EVT_PUSH,
		EVT_POP,
		EVT_REDRAW,
 	}event_t;

	typedef enum  {
		TRANS_ENTERING = 0,
		TRANS_REFRESH,
		TRANS_IDLE,
		TRANS_LEAVING,
	}modeTransition_t;
		
	bool isMultiPage(mode_state_t mode);
	bool processSelectorKnobAction( knob_action_t action);
	
	uint8_t pageCountForMode(mode_state_t mode);
 
	void drawMode(modeTransition_t transition, mode_state_t mode, string eventArg = "");
	void drawStartupScreen(modeTransition_t transition);
	void drawDeviceStatusScreen(modeTransition_t transition);
	void drawTimeScreen(modeTransition_t transition);
	void drawVolumeScreen(modeTransition_t transition);
	void drawBalanceScreen(modeTransition_t transition);
	bool processSelectorKnobActionForBalance( knob_action_t action);
	bool processSelectorKnobActionForDTC( knob_action_t action);
 
	void drawRadioScreen(modeTransition_t transition);
	void drawGPSScreen(modeTransition_t transition);
	
	void drawCANBusScreen(modeTransition_t transition);
	void drawCANBusScreen1(modeTransition_t transition);

 	void drawDTCScreen(modeTransition_t transition);
 	void drawDTCInfoScreen(modeTransition_t transition, string code);
	bool processSelectorKnobActionForDTCInfo( knob_action_t action);
 
	void drawSettingsScreen(modeTransition_t transition);
	
	void drawInfoScreen(modeTransition_t transition);
 
	void drawInternalError(modeTransition_t transition);
 
	void drawShutdownScreen();
	void drawDeviceStatus();
	
	void drawEngineCheck();
	void drawTimeBox();
	
// display value formatting
 	bool normalizeCANvalue(string key, string & value);
	
//Menu stuff
	void resetMenu();
	bool menuSelectAction(knob_action_t action);
	void drawMenuScreen(modeTransition_t transition);
	vector<menuItem_t>	_menuItems;
	int						_currentMenuItem;
	int						_menuCursor;			// item at top of screen

	time_t					 _menuTimeout;
	menuSelectedCallBack_t _menuCB;
	string					  _menuTitle;
//
	
	
	uint8_t   	_lineOffset = 0;	// used for multi-line
	
	mode_state_t _current_mode = MODE_UNKNOWN;
	uint8_t		 _currentPage  = 0;			// used for multipage
	mode_state_t _saved_mode   = MODE_UNKNOWN;
	
	timeval		_lastEventTime = {0,0};
 
	mode_state_t handleRadioEvent();
	bool isStickyMode(mode_state_t);
	bool pushMode(mode_state_t);
	void popMode();
	
	void setEvent(event_t event, mode_state_t mode = MODE_UNKNOWN, string arg = "");
 
	// LED effects Bit map
	
#define LED_EVENT_ALL					0xFFFFFFFF
	
#define LED_EVENT_STARTUP				0x00000001
#define LED_EVENT_VOL 					0x00000002
#define LED_EVENT_MUTE					0x00000004
#define LED_EVENT_STOP					0x00008000
	
#define LED_EVENT_STARTUP_RUNNING	0x00010000
#define LED_EVENT_VOL_RUNNING			0x00020000
#define LED_EVENT_MUTE_RUNNING		0x00040000

	uint32_t  _ledEvent  = 0;
	void ledEventSet(uint32_t set, uint32_t reset);
	void ledEventUpdate();
	void runLEDEventStartup();
	void runLEDEventVol();
	void runLEDEventMute();

	void DisplayUpdate();		// C++ version of thread
	// C wrappers for DisplayUpdate;
	static void* DisplayUpdateThread(void *context);
	static void DisplayUpdateThreadCleanup(void *context);
 
	typedef struct {
		event_t			evt :8;
		mode_state_t	mode:8;
		string			arg;
	}  eventQueueItem_t;

	queue<eventQueueItem_t> _eventQueue; // upper 8 bits is mode . lower 8 is event
	 
	pthread_cond_t 	_cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t 	_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_t			_updateTID;
	bool 					_isRunning = false;
 
 	// display
	bool 					_isSetup = false;
	
	// colors and brightness
	RGB 					_rightKnobColor;
	RGB 					_leftKnobColor;
	uint8_t				_dimLevel;
	

	// devices
	VFD 					_vfd;
	DuppaLEDRing		_rightRing;
	DuppaLEDRing		_leftRing;
	DuppaKnob			_leftKnob;
	DuppaKnob			_rightKnob;
 
};

