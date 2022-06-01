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
	_packetCount = {};
	_lastFrameTime  = {};
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

// MARK: -  ODB polling

bool CANBusMgr::request_ODBpolling(string key){
	bool success = false;
	
	if( _odb_polling.find(key) == _odb_polling.end()){
		
		
		vector<uint8_t>  request;
		if( _frameDB.odb_request(key, request)) {
			
			odb_polling_t poll_info;
			poll_info.request = request;
			
			_odb_polling[key] = poll_info;
			
			printf("ODB request %s\n", key.c_str());
			
			success = true;
		}
	}
	
	return success;
}

bool CANBusMgr::cancel_ODBpolling(string key){
	bool success = false;

	
	if( _odb_polling.find(key) == _odb_polling.end()){
		_odb_polling.erase(key);
		
		printf("ODB cancel %s \n", key.c_str());
		success = true;

	}
	return success;
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
	
	{
		printf("pollableInterfaces ");
		for( auto e : _frameDB.pollableInterfaces()){
			printf("%s ", e.c_str());
		}
		printf("\n");
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
			
			if(_packetCount.count(key))
				stat.packetCount = _packetCount[key];
			
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
			
			timeOut = time;
			return true;
		}
	}
	return false;
}

bool CANBusMgr::packetCount(string ifName, size_t &countOut){
	
	size_t totalCount = 0;
	
	// close all?
	if(ifName.empty()){
		for (auto& [_, count]  : _packetCount){
			totalCount += count;
		}
		countOut = totalCount;
 		return true;
	}
	else for (auto& [key, count]  : _packetCount){
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
		_packetCount = {};
 		return true;
	}
	else for (auto& [key, count]  : _packetCount){
		if (strcasecmp(key.c_str(), ifName.c_str()) == 0){
			_packetCount[key] = 0;
 			return true;
		}
	}
	return false;
}

// MARK: -  CANReader thread

void CANBusMgr::CANReader(){
	 
 	
	while(_isRunning){
		
		// if not setup // check back later
		if(!_isSetup){
			sleep(10);
			continue;
		}
	 
		/* wait for something to happen on the socket */
		struct timeval selTimeout;
		selTimeout.tv_sec = 0;       /* timeout (secs.) */
		selTimeout.tv_usec = 100;            /* 100 microseconds */
		
		/* back up master */
		fd_set dup = _master_fds;
		
		int numReady = select(_max_fds+1, &dup, NULL, NULL, &selTimeout);
		if( numReady == -1 ) {
			perror("select");
	//		_running = false;
		}
		
		/* check which fd is avaialbe for read */
		for (auto& [ifName, fd]  : _interfaces) {
			if ((fd != -1)  && FD_ISSET(fd, &dup)) {
				
				struct can_frame frame;
		 
				struct timeval tv;
				gettimeofday(&tv, NULL);
				
	 			unsigned long timestamp = (tv.tv_sec * 100 ) + (tv.tv_usec / 10000);
 
				size_t nbytes = read(fd, &frame, sizeof(struct can_frame));
				
				if(nbytes == 0){ // shutdown
					_interfaces[ifName] = -1;
				}
				else if(nbytes > 0){
					_frameDB.saveFrame(ifName, frame, timestamp);
					_lastFrameTime[ifName] =  tv.tv_sec;
					_packetCount[ifName]++;
				}
			}
		}

		
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
 
