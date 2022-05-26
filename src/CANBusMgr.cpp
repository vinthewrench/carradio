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

	pthread_create(&_TID, NULL,
										  (THREADFUNCPTR) &CANBusMgr::CANReaderThread, (void*)this);


}

CANBusMgr::~CANBusMgr(){
	
	int error;

	stop("", error);
	
	FD_ZERO(&_master_fds);
	_max_fds = 0;

	pthread_mutex_lock (&_mutex);
	_isRunning = false;
	pthread_cond_signal(&_cond);
	pthread_mutex_unlock (&_mutex);
	pthread_join(_TID, NULL);
 }

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
					_isRunning = true;
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
 
