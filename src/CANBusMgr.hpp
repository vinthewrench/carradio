//
//  CANBusMgr.hpp
//  canhacker
//
//  Created by Vincent Moscaritolo on 3/22/22.
//
#pragma once


#include <unistd.h>
#include <sys/time.h>
#include <random>

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <mutex>
#include <thread>			//Needed for std::thread
#include <fstream>
#include <pthread.h>
#include <time.h>

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
	
	bool lastFrameTime(string ifName, time_t &time);
	bool totalPacketCount(string ifName, size_t &count);
	bool packetsPerSecond(string ifName, size_t &count);

	bool resetPacketCount(string ifName);
	
	bool sendFrame(string ifName, canid_t can_id, vector<uint8_t> bytes,  int *error = NULL);
	
	typedef struct {
		string 	ifName;
		time_t	lastFrameTime;
		size_t	packetCount;
	} can_status_t;
	
	bool getStatus(vector<can_status_t> & stats);
	
	FrameDB* frameDB() {return &_frameDB;};
	
	bool queue_OBDPacket(vector<uint8_t> request);

	bool request_OBDpolling(string key);
	bool cancel_OBDpolling(string key);
	bool sendDTCEraseRequest();
	
private:
	
	bool 				_isSetup = false;
	FrameDB			_frameDB;
	
	
	void 				CANReader();		// C++ version of thread
	// C wrappers for CANReader;
	static void* 	CANReaderThread(void *context);
	static void 	CANReaderThreadCleanup(void *context);
	bool 				_isRunning = false;
	pthread_t		_TID;
	
	int				openSocket(string ifName, int &error);
	void 				processOBDrequests();

	map<string, int> 		_interfaces = {};
	map<string, time_t> 	_lastFrameTime = {};
	map<string, size_t> 	_totalPacketCount = {};
	
	map<string, size_t> 	_runningPacketCount = {};
	map<string, time_t> 	_avgPacketsPerSecond = {};

	typedef struct {
		vector<uint8_t> request;
		bool repeat;
	} obd_polling_t;
	
	map<string, obd_polling_t> 	_obd_polling = {};
	vector<obd_polling_t> 			_obd_requests = {};

	struct timespec		_lastPollTime;
	int64_t     			_pollDelay;			// how long to wait before next OBD poll in milliseconds
	vector<string> 		_keysToPoll = {};

	fd_set					_master_fds;		// Can sockets that are ready for read
	int						_max_fds;
	
	mt19937						_rng;

};

