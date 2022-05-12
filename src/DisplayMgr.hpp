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

	void showStartup();
	void showVolumeChange();
	void showBalanceChange();
	void showRadioChange();

 	// Menu Screen Management
	typedef vector< pair <uint, string>>  menuItems_t;
	typedef std::function<void(bool didSucceed, uint selectedItemID)> menuSelectedCallBack_t;
	void showMenuScreen(menuItems_t items, uint intitialItem,  time_t timeout = 0,  menuSelectedCallBack_t cb = nullptr);
 
	typedef enum  {
		MENU_EXIT = 0,
		MENU_UP,
		MENU_DOWN,
		MENU_CLICK
	}menu_action;
	void menuSelectAction(menu_action action);
	bool isMenuDisplayed() {return _current_mode == MODE_MENU;};
	
	
private:
	void drawCurrentMode(bool redraw, uint16_t event);
	void drawStartupScreen(bool redraw, uint16_t event);
	void drawTimeScreen(bool redraw, uint16_t event);
	void drawVolumeScreen(bool redraw, uint16_t event);
	void drawBalanceScreen(bool redraw, uint16_t event);
	void drawRadioScreen(bool redraw, uint16_t event);
	void drawDiagScreen(bool redraw, uint16_t event);
	void drawInternalError(bool redraw, uint16_t event);

//Menu stuff
	void resetMenu();
	void drawMenuScreen(bool redraw, uint16_t event);
	menuItems_t				 _menuItems;
	uint						 _currentMenuItem;
	time_t					 _menuTimeout;
	menuSelectedCallBack_t _menuCB;
//
	
	typedef enum  {
		MODE_UNKNOWN = 0,
		MODE_STARTUP,
		MODE_TIME,
		MODE_VOLUME,
		MODE_BALANCE,
		MODE_RADIO,
		MODE_DIAG,
		MODE_MENU,
		MODE_SHUTDOWN,		// shutdown
	}mode_state_t;

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

#define DISPLAY_EVENT_STARTUP	0x0001
#define DISPLAY_EVENT_EXIT 	0x0002
#define DISPLAY_EVENT_POP	 	0x0004

#define DISPLAY_EVENT_TIME 	0x0008
#define DISPLAY_EVENT_VOLUME 	0x0010
#define DISPLAY_EVENT_BALANCE 0x0020
#define DISPLAY_EVENT_RADIO 	0x0040
	
#define DISPLAY_EVENT_DIAG 	0x0100

#define DISPLAY_EVENT_MENU 			0x1000
#define DISPLAY_EVENT_MENU_CHANGED 	0x2000

	uint16_t				_event = 0;
	 
	pthread_cond_t 	_cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t 	_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_t			_updateTID;
	
 	// display
	bool 					_isSetup = false;
	VFD 					_vfd;
	// debug stuff
	
//	string modeString();

};

