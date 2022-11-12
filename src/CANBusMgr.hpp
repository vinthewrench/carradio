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
	
	// ISOTP  handlers
 	typedef std::function<void(void* context,
										string ifName, canid_t can_id, vector<uint8_t> bytes,
										unsigned long timeStamp)> ISOTPHandlerCB_t;

	bool registerISOTPHandler(string ifName, canid_t can_id,  ISOTPHandlerCB_t  cb = NULL, void* context = NULL);
	
	void unRegisterISOTPHandler(string ifName, canid_t can_id, ISOTPHandlerCB_t cb );
	
	vector<pair<ISOTPHandlerCB_t, void*>>	handlerForCanID(string ifName, canid_t can_id);
	
	bool sendISOTP(string ifName, canid_t can_id,  vector<uint8_t> bytes,  int* error = NULL );

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
	
	typedef uint32_t periodicCallBackID_t;

	typedef std::function<bool(void* context,  canid_t &can_id, vector<uint8_t> &bytes)> periodicCallBack_t;
	bool setPeriodicCallback (string ifName, int64_t delay,
									  periodicCallBackID_t & callBackID,
									  void* context,
									  periodicCallBack_t cb);
	bool removePeriodicCallback (periodicCallBackID_t callBackID );
 
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
	void 				processPeriodicRequests();

	void				processISOTPFrame(string ifName, can_frame_t frame, unsigned long  timeStamp);
 
	map<string, int> 		_interfaces = {};
	map<string, time_t> 	_lastFrameTime = {};
	map<string, size_t> 	_totalPacketCount = {};
	
	map<string, size_t> 	_runningPacketCount = {};
	map<string, time_t> 	_avgPacketsPerSecond = {};

	typedef struct {
		periodicCallBackID_t taskID;
		string 					ifName;
		int64_t				 	delay;
		struct timespec		lastRun;
		void* 					context; //passed to cb
		periodicCallBack_t 	cb;
	} periodic_task_t;

	map<periodicCallBackID_t, periodic_task_t> 	_periodic_tasks = {};
	
	typedef struct {
		vector<uint8_t> request;
		bool repeat;
	} obd_polling_t;
	
	map<string, obd_polling_t> 	_obd_polling = {};
	vector<obd_polling_t> 			_obd_requests = {};

	typedef struct {
		string 				ifName;
		canid_t 				can_id;
		ISOTPHandlerCB_t	cb;
		void					*context;
	} frame_handler_t;
 
	vector<frame_handler_t> _frame_handlers= {};

	// for sending larger ISOTP data
	typedef struct {
 		string 				ifName;
		canid_t				can_id;
		
		vector<uint8_t> 	bytes;				// message bytes
		uint16_t			 	bytes_sent;;		// offset into next
		uint8_t				separation_delay;
		struct timespec	lastSentTime;
	} isotp_state_t;

	//collection of packets waiting to go.  Note that adding a new one for same hash causes us to reset
	map <uint32_t, isotp_state_t> _waiting_isotp_packets = {};
	mutable std::mutex _isotp_mutex;

	struct timespec		_lastPollTime;
	int64_t     			_pollDelay;			// how long to wait before next OBD poll in milliseconds
	vector<string> 		_keysToPoll = {};

	fd_set					_master_fds;		// Can sockets that are ready for read
	int						_max_fds;
	
	mt19937						_rng;

};

