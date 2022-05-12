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

using namespace std;


class DisplayMgr {
	
public:

	DisplayMgr();
	~DisplayMgr();
		
	bool begin(const char* path, speed_t speed =  B19200);
	bool begin(const char* path, speed_t speed, int &error);
	void stop();

	 bool reset();
		
	bool setBrightness(uint8_t level);  // 0-7

	void showTime();
	void showDiag();
	void showGPS();
	void showStartup();
	void showVolumeChange();
	void showBalanceChange();
	void showRadioChange();

 	// Menu Screen Management
	typedef string menuItem_t;
	typedef std::function<void(bool didSucceed, uint selectedItemID)> menuSelectedCallBack_t;
	void showMenuScreen(vector<menuItem_t> items, uint intitialItem,  time_t timeout = 0,  menuSelectedCallBack_t cb = nullptr);
 
	typedef enum  {
		MENU_EXIT = 0,
		MENU_UP,
		MENU_DOWN,
		MENU_CLICK
	}menu_action;
	void menuSelectAction(menu_action action);
	bool isMenuDisplayed() {return _current_mode == MODE_MENU;};
	
	
private:
		
	typedef enum  {
		MODE_UNKNOWN = 0,
		MODE_STARTUP,
		MODE_TIME,
		MODE_VOLUME,
		MODE_BALANCE,
		MODE_RADIO,
		MODE_DIAG,
		MODE_GPS,
	
		MODE_MENU,
 		MODE_SHUTDOWN,		// shutdown
	}mode_state_t;

	typedef enum  {
		EVT_NONE = 0,
		EVT_PUSH,
		EVT_POP,
 	}event_t;

	void drawCurrentMode(bool redraw, bool shouldUpdate);
	void drawStartupScreen(bool redraw, bool shouldUpdate);
	void drawTimeScreen(bool redraw, bool shouldUpdate);
	void drawVolumeScreen(bool redraw, bool shouldUpdate);
	void drawBalanceScreen(bool redraw, bool shouldUpdate);
	void drawRadioScreen(bool redraw, bool shouldUpdate);
	void drawDiagScreen(bool redraw, bool shouldUpdate);
	void drawGPSScreen(bool redraw, bool shouldUpdate);
	void drawInternalError(bool redraw, bool shouldUpdate);

//Menu stuff
	void resetMenu();
	void drawMenuScreen(bool redraw, bool shouldUpdate);
	vector<menuItem_t>	_menuItems;
	int						_currentMenuItem;
	uint						_menuCursor;

	time_t					 _menuTimeout;
	menuSelectedCallBack_t _menuCB;
//

	
	mode_state_t _current_mode = MODE_UNKNOWN;
	mode_state_t _saved_mode   = MODE_UNKNOWN;
	timeval		_lastEventTime = {0,0};
 
	mode_state_t handleRadioEvent();
	bool isStickyMode(mode_state_t);
	bool pushMode(mode_state_t);
	void popMode();
	
	void setEvent(event_t event, mode_state_t mode = MODE_UNKNOWN);
 
	void DisplayUpdate();		// C++ version of thread
	// C wrappers for DisplayUpdate;
	static void* DisplayUpdateThread(void *context);
	static void DisplayUpdateThreadCleanup(void *context);
 
	typedef struct {
		event_t			evt :8;
		mode_state_t	mode:8;
	}  eventQueueItem_t;

	queue<eventQueueItem_t> _eventQueue; // upper 8 bits is mode . lower 8 is event
	 
	pthread_cond_t 	_cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t 	_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_t			_updateTID;
	
 	// display
	bool 					_isSetup = false;
	VFD 					_vfd;
	// debug stuff
	
//	string modeString();

};

