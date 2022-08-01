//
//  W1Mgr.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 7/28/22.
//
#pragma once
 

#include <mutex>
#include <utility>      // std::pair, std::make_pair
#include <string>       // std::string
#include <queue>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <pthread.h>
#include "CommonDefs.hpp"


using namespace std;

class W1Mgr {
	
public:

	W1Mgr();
	~W1Mgr();
 
	bool begin(uint64_t queryDelay = 5 /* seconds*/ );
	bool begin(uint64_t queryDelay,  int &error);
	 
	void setQueryDelay(uint64_t);

	bool setShouldRead(bool shouldRead);
	bool shouldRead() {return _shouldRead;};
 
	void stop();
	bool isConnected() ;
 
	bool 	getTemperatures(map<string,float>& );
	
	
private:
	bool 				_isSetup = false;
	bool				_shouldRead = false;
 
	struct timespec	_lastQueryTime;
	uint64_t     		_queryDelay;			// how long to wait before next query
 
	map<string,float>	_temperatureData;
	
	stringvector	getDeviceIDs();
	stringvector	getW1_slaveInfo(string deviceName);
	bool				processDS18B20(string deviceName);
	
	void W1Reader();		// C++ version of thread
	// C wrappers for W1Reader;
	static void* W1ReaderThread(void *context);
	static void W1ReaderThreadCleanup(void *context);
	bool 			_isRunning = false;
 
  pthread_cond_t 		_cond = PTHREAD_COND_INITIALIZER;
  pthread_mutex_t 	_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_t				_TID;
};
