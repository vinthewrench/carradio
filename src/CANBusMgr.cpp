//
//  CANBusMgr.cpp
//  canhacker
//
//  Created by Vincent Moscaritolo on 3/22/22.
//

#include "CANBusMgr.hpp"
#include <iostream>
#include <sstream>
#include <cassert>
#include "Utils.hpp"
#include <random>
#include <stdlib.h>
#include <stdint.h>
#include <array>
#include <climits>
#include "timespec_util.h"


using namespace std;

/* add a fd to fd_set, and update max_fd */
static int safe_fd_set(int fd, fd_set* fds, int* max_fd) {
	 assert(max_fd != NULL);

	 FD_SET(fd, fds);
	 if (fd > *max_fd) {
		  *max_fd = fd;
	 }
	 return 0;
}

/* clear fd from fds, update max fd if needed */
static int safe_fd_clr(int fd, fd_set* fds, int* max_fd) {
	 assert(max_fd != NULL);

	 FD_CLR(fd, fds);
	 if (fd == *max_fd) {
		  (*max_fd)--;
	 }
	 return 0;
}
 
typedef void * (*THREADFUNCPTR)(void *);

CANBusMgr::CANBusMgr(){
	_interfaces.clear();
	FD_ZERO(&_master_fds);
	_max_fds = 0;
	
	_isSetup = false;
	_isRunning = true;
	_totalPacketCount = {};
	_lastFrameTime  = {};
	_runningPacketCount = {};
	_avgPacketsPerSecond = {};

	_lastPollTime = {0,0};
	_pollDelay = 500 ; //  500 milliseconds
 
	_obd_requests = {};
	_obd_polling = {};
	
	// create RNG engine
	constexpr std::size_t SEED_LENGTH = 8;
  std::array<uint_fast32_t, SEED_LENGTH> random_data;
  std::random_device random_source;
  std::generate(random_data.begin(), random_data.end(), std::ref(random_source));
  std::seed_seq seed_seq(random_data.begin(), random_data.end());
	_rng =  std::mt19937{ seed_seq };
 
	pthread_create(&_TID, NULL,
										  (THREADFUNCPTR) &CANBusMgr::CANReaderThread, (void*)this);

}

CANBusMgr::~CANBusMgr(){
	
	int error;

	stop("", error);
	
	FD_ZERO(&_master_fds);
	_max_fds = 0;

	
	_isRunning = false;
	pthread_join(_TID, NULL);
 }

// MARK: -  CANReader Handlers

bool CANBusMgr::registerHandler(string ifName) {
	
	// is it an already registered ?
	if(_interfaces.count(ifName))
		return false;
	
	_interfaces[ifName] = -1;
	
	return true;
}

void CANBusMgr::unRegisterHandler(string ifName){
	
	if(_interfaces.count(ifName)){
		
		int error;
		
//		auto ifInfo = _interfaces[ifName];
		stop(ifName, error);
		_interfaces.erase(ifName);
	}
	
}

bool CANBusMgr::registerProtocol(string ifName,  CanProtocol *protocol){
	bool success = false;

	if(!protocol)
		throw Exception("ifName is blank");

	if(_frameDB.registerProtocol(ifName, protocol) ){
		protocol->registerSchema(this);
		success = true;
	}
	return success;
}

// MARK: -  OBD polling



bool CANBusMgr::queue_OBDPacket(vector<uint8_t> request){
 
	std::uniform_int_distribution<long> distribution(LONG_MIN,LONG_MAX);
	
	obd_polling_t poll_info;
	poll_info.request = request;
	poll_info.repeat = false;
	string key = to_string( distribution(_rng));
	
	_obd_polling[key] = poll_info;
 	return true;
}


bool CANBusMgr::request_OBDpolling(string key){
	bool success = false;
	
	if( _obd_polling.find(key) == _obd_polling.end()){
 
		vector<uint8_t>  request;
		if( _frameDB.obd_request(key, request)) {
		
//			printf("REQUEST %s\n", key.c_str());

			obd_polling_t poll_info;
			poll_info.request = request;
			poll_info.repeat = true;
			
			_obd_polling[key] = poll_info;
			
			success = true;
		}
	}
	
	return success;
}

bool CANBusMgr::cancel_OBDpolling(string key){

	if( _obd_polling.count(key)){
//		printf("CANCEL %s\n", key.c_str());
		_obd_polling.erase(key);
	}

	return true;
}

bool CANBusMgr::sendDTCEraseRequest(){
 	vector<uint8_t> obd_request = {0x01, 0x04 };  //Clear Diagnostic Trouble Codes and stored values
	return queue_OBDPacket(obd_request);
}



// MARK: -  CANReader control

bool CANBusMgr::start(string ifName, int &error){
	
	for (auto& [key, fd]  : _interfaces){
		if (strcasecmp(key.c_str(), ifName.c_str()) == 0){
 			if(fd == -1){
				// open connection here
				fd = openSocket(ifName, error);
				_interfaces[ifName] = fd;
				
				if(fd < 0){
					error = errno;
					return false;
				}
				else {
					_isSetup = true;
					return true;;
				}
 			}
			else {
				// already open
				return true;
			}
		}
	}
		
	error = ENXIO;
	return false;
}

int CANBusMgr::openSocket(string ifname, int &error){
	int fd = -1;
	
	// create a socket
	fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if(fd == -1){
		error = errno;
		return -1;
	}
	
	// Get the index number of the network interface
	unsigned int ifindex = if_nametoindex(ifname.c_str());
	if (ifindex == 0) {
		error = errno;
		return -1;
	}

	// fill out Interface request structure
	struct sockaddr_can addr;
	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifindex;

	if (::bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		error = errno;
		return -1;
	}
	
	// add to read set
	safe_fd_set(fd, &_master_fds, &_max_fds);
	
//	printf("open PF_CAN %s = %d\n", ifname.c_str(),  fd);

	return fd;
}



bool CANBusMgr::getStatus(vector<can_status_t> & statsOut){
 
	vector<can_status_t> stats = {};
	
	for (auto& [key, fd]  : _interfaces){
		if(fd != -1){
			can_status_t stat;
			stat.ifName = key;
			
			if(_lastFrameTime.count(key))
				stat.lastFrameTime = _lastFrameTime[key];
			
			if(_totalPacketCount.count(key))
				stat.packetCount = _totalPacketCount[key];
			
			stats.push_back(stat);
 		}
		
	}
	statsOut = stats;
	
	return stats.size() > 0;;
}

#warning may want to toggle _isRunning = false here
bool CANBusMgr::stop(string ifName, int &error){
	
	// close all?
	if(ifName.empty()){
		for (auto& [key, fd]  : _interfaces){
			if(fd != -1){
				close(fd);
				safe_fd_clr(fd, &_master_fds, &_max_fds);
				_interfaces[key] = -1;
			}
		}
		return true;
 	}
	else for (auto& [key, fd]  : _interfaces){
		if (strcasecmp(key.c_str(), ifName.c_str()) == 0){
			if(fd != -1){
				close(fd);
				safe_fd_clr(fd, &_master_fds, &_max_fds);
				_interfaces[ifName] = -1;
			}
			return true;
		}
	}
	error = ENXIO;
	return false;
}


bool CANBusMgr::sendFrame(string ifName, canid_t can_id, vector<uint8_t> bytes,  int *errorOut){

	int error = EBADF;
	 
	if (bytes.size() < 1 || bytes.size() > 8) {
		error = EMSGSIZE;
 	}
//	else if (can_id > 0 )  {
//		error = EBADF;
//	}
	else if(!ifName.empty()){
		for (auto& [key, fd]  : _interfaces){
			if (strcasecmp(key.c_str(), ifName.c_str()) == 0){
				if(fd != -1){
						// create packet
	 
					struct can_frame frame;
					memset(&frame, 0, sizeof frame);
					
					frame.can_id = can_id;
					for(int i = 0; i < bytes.size();  i++)
						frame.data[i]  = bytes[i];
					frame.can_dlc = bytes.size();
					
					if( write(fd, &frame, CAN_MTU) == CAN_MTU) return true;
					
					error  = errno;
				}
				break;
			}
		}
	}
	
	if(errorOut) *errorOut = error;
	return false;
}
 


bool CANBusMgr::lastFrameTime(string ifName, time_t &timeOut){
	
	time_t lastTime = 0;
	
	
	// close all?
	if(ifName.empty()){
		for (auto& [key, time]  : _lastFrameTime){
			if(time > lastTime){
				lastTime = time;
			}
		}
		timeOut = lastTime;
		return true;
	}
	else for (auto& [key, time]  : _lastFrameTime){
		if (strcasecmp(key.c_str(), ifName.c_str()) == 0){
			
			printf("lastFrameTime %s %d\n", ifName.c_str(), (int) time);

			timeOut = time;
			return true;
		}
	}
	return false;
}

bool CANBusMgr::totalPacketCount(string ifName, size_t &countOut){
	
	size_t totalCount = 0;
	
	// close all?
	if(ifName.empty()){
		for (auto& [_, count]  : _totalPacketCount){
			totalCount += count;
		}
		countOut = totalCount;
 		return true;
	}
	else for (auto& [key, count]  : _totalPacketCount){
		if (strcasecmp(key.c_str(), ifName.c_str()) == 0){
			
			countOut = count;
			return true;
		}
	}
	return false;
}


bool CANBusMgr::packetsPerSecond(string ifName, size_t &countOut){
	size_t totalCount = 0;
	
 // average total
	if(ifName.empty()){
		for (auto& [_, count]  : _avgPacketsPerSecond){
			totalCount += count;
		}
		
		countOut = 0;
		if(_avgPacketsPerSecond.size() > 0){
			countOut = totalCount / _avgPacketsPerSecond.size();
		}
		
		return true;
	}
	else for (auto& [key, count]  : _avgPacketsPerSecond){
		if (strcasecmp(key.c_str(), ifName.c_str()) == 0){
			
			countOut = count;
			return true;
		}
	}
	return false;

}


bool CANBusMgr::resetPacketCount(string ifName){
	 
	// close all?
	if(ifName.empty()){
		_totalPacketCount = {};
		_runningPacketCount = {};
		_avgPacketsPerSecond = {};
  		return true;
	}
	else for (auto& [key, count]  : _totalPacketCount){
		if (strcasecmp(key.c_str(), ifName.c_str()) == 0){
			_totalPacketCount[key] = 0;
			_runningPacketCount[key] = 0;
			_avgPacketsPerSecond[key]  = 0;
 			return true;
		}
	}
	return false;
}

// MARK: -  CANReader thread

 
void CANBusMgr::CANReader(){
	 
	PRINT_CLASS_TID;
	
	struct timespec lastTime;
	clock_gettime(CLOCK_MONOTONIC, &lastTime);

 	while(_isRunning){
		
		// if not setup // check back later
		if(!_isSetup){
			sleep(2);
			continue;
		}
		
		/* wait for something to happen on the socket */
		struct timeval selTimeout;
		selTimeout.tv_sec = 0;       /* timeout (secs.) */
		selTimeout.tv_usec = 200000;            /* 200000 microseconds */
		
		/* back up master */
		fd_set dup = _master_fds;
		
		int numReady = select(_max_fds+1, &dup, NULL, NULL, &selTimeout);
		if( numReady == -1 ) {
			perror("select");
	//		_running = false;
		}
		
		struct timespec now, diff;
		clock_gettime(CLOCK_MONOTONIC, &now);
		timespec_sub( &diff, &now, &lastTime);
		int64_t timestamp_secs = timespec_to_msec(&now) /1000;
		
		/* check which fd is avail for read */
		for (auto& [ifName, fd]  : _interfaces) {
			if ((fd != -1)  && FD_ISSET(fd, &dup)) {
				
				struct can_frame frame;
		 
				size_t nbytes = read(fd, &frame, sizeof(struct can_frame));
				
				if(nbytes == 0){ // shutdown
					_interfaces[ifName] = -1;
				}
				else if(nbytes > 0){					
					_frameDB.saveFrame(ifName, frame, timestamp_secs);
					_lastFrameTime[ifName] =  now.tv_sec;
					_totalPacketCount[ifName]++;
					_runningPacketCount[ifName]++;
				}
			}
		}
		
		// did more than a second go by
		if(timespec_to_msec(&diff) > 1000){
			lastTime = now;
	 
			// calulate avareage
			for (auto& [ifName, fd]  : _interfaces) {
				_avgPacketsPerSecond[ifName] = 	(_runningPacketCount[ifName] + _avgPacketsPerSecond[ifName] ) / 2;
				_runningPacketCount[ifName]  = 0;
			}
 		}
 
		// process any needed OBD requests 
		processOBDrequests();
	}
}

void CANBusMgr::processOBDrequests() {
	auto ifNames =  _frameDB.pollableInterfaces();
	if(ifNames.empty())
		return;
	
	bool shouldQuery = false;
	
	if(_lastPollTime.tv_sec == 0 &&  _lastPollTime.tv_nsec == 0 ){
		shouldQuery = true;
	} else {
		
		struct timespec now, diff;
		clock_gettime(CLOCK_MONOTONIC, &now);
		timespec_sub( &diff, &now, &_lastPollTime);
		
		if(timespec_to_msec(&diff) >= _pollDelay){
			shouldQuery = true;
		}
	}
	
	if(shouldQuery){
		
		// walk any open interfaces and find the onse that are pollable
		for (auto& [key, fd]  : _interfaces){
			if(fd != -1){
				if (find(ifNames.begin(), ifNames.end(), key) != ifNames.end()){
					
					if(_keysToPoll.empty())
						_keysToPoll = all_keys(_obd_polling);
					
					if(!_keysToPoll.empty()){
						auto obdKey = _keysToPoll.back();
						_keysToPoll.pop_back();
						if( _obd_polling.find(obdKey) != _obd_polling.end()){
							
							auto pInfo = 	_obd_polling[obdKey];
							
							// send out a frame
							sendFrame(key, 0x7DF, pInfo.request);
							
#if 0
							string keyname = pInfo.repeat?string(obdKey):"ONE-TIME";
							printf("send(%s) OBD %10s ", key.c_str(), keyname.c_str());
							for(auto i = 0; i < pInfo.request.size() ; i++)
								printf("%02x ",pInfo.request[i]);
							printf("\n");
#endif
							
							// remove any non repeaters
							if(pInfo.repeat == false){
								cancel_OBDpolling(obdKey);
							}
						}
						
					}
				};
			}
		}
		clock_gettime(CLOCK_MONOTONIC, &_lastPollTime);
	}
}


void* CANBusMgr::CANReaderThread(void *context){
	CANBusMgr* d = (CANBusMgr*)context;

	//   the pthread_cleanup_push needs to be balanced with pthread_cleanup_pop
	pthread_cleanup_push(   &CANBusMgr::CANReaderThreadCleanup ,context);
 
	d->CANReader();
	
	pthread_exit(NULL);
	
	pthread_cleanup_pop(0);
	return((void *)1);
}

 
void CANBusMgr::CANReaderThreadCleanup(void *context){
	//CANBusMgr* d = (CANBusMgr*)context;
 
	printf("cleanup GPSRCANReadereader\n");
}
 
