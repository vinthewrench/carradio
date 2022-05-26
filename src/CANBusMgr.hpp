//
//  CANBusMgr.hpp
//  canhacker
//
//  Created by Vincent Moscaritolo on 3/22/22.
//
#pragma once


#include <unistd.h>
#include <sys/time.h>

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <mutex>
#include <thread>			//Needed for std::thread
#include <fstream>
#include <pthread.h>

#include <unistd.h>
#include <sys/time.h>

#include "CommonDefs.hpp"
#include "FrameDB.hpp"
#include "CanProtocol.hpp"

using namespace std;
 
class CANBusMgr {
	
public:
 
	CANBusMgr();
  ~CANBusMgr();
 	
	bool registerHandler(string ifName);
	void unRegisterHandler(string ifName);

	bool registerProtocol(string ifName,  CanProtocol *protocol = NULL);

	bool start(string ifName, int &error);
	bool stop(string ifName, int &error);

	bool sendFrame(string ifName, canid_t can_id, vector<uint8_t> bytes,  int *error = NULL);
	
	FrameDB* frameDB() {return &_frameDB;};
 
private:
	
	bool 				_isSetup = false;

	FrameDB			_frameDB;
 
	
	pthread_cond_t 		_cond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t 		_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_t				_TID;

	void 				CANReader();		// C++ version of thread
	// C wrappers for CANReader;
	static void* 	CANReaderThread(void *context);
	static void 	CANReaderThreadCleanup(void *context);
	bool 				_isRunning = false;

	int openSocket(string ifName, int &error);

	map<string, int> _interfaces;
	fd_set	 _master_fds;		// Can sockets that are ready for read
	int		_max_fds;
};

