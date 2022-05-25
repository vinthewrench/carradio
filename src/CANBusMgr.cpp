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

#include "GMLAN.hpp"
#include  "Wranger2010.hpp"

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
 
CANBusMgr::CANBusMgr(){
	_interfaces.clear();
	_running = true;
	FD_ZERO(&_master_fds);
	_max_fds = 0;
	
	_thread = std::thread(&CANBusMgr::CANThread, this);

}

CANBusMgr::~CANBusMgr(){
	
	stop("");
	
	FD_ZERO(&_master_fds);
	_max_fds = 0;
	_running = false;
	_reading = false;
	_paused = false;
	
	
 if (_thread.joinable())
		_thread.join();
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
//		auto ifInfo = _interfaces[ifName];
		stop(ifName);
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


bool CANBusMgr::start(string ifName,int *errorOut){
	
	for (auto& [key, fd]  : _interfaces){
		if (strcasecmp(key.c_str(), ifName.c_str()) == 0){
 			if(fd == -1){
				// open connection here
				fd = openSocket(ifName, errorOut);
				_interfaces[ifName] = fd;
 				return fd == -1?false:true;
			}
			else {
				// already open
				return true;
			}
		}
	}
	
	if(errorOut) *errorOut = ENXIO;
	return false;
}

int CANBusMgr::openSocket(string ifname, int *errorOut){
	int fd = -1;
	
	// create a socket
	fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if(fd == -1){
		if(errorOut) *errorOut = errno;
		return -1;
	}
	
	// Get the index number of the network interface
	unsigned int ifindex = if_nametoindex(ifname.c_str());
	if (ifindex == 0) {
		if(errorOut) *errorOut = errno;
		return -1;
	}

	// fill out Interface request structure
	struct sockaddr_can addr;
	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifindex;

	if (::bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		if(errorOut) *errorOut = errno;
		return -1;
	}
	
	// add to read set
	safe_fd_set(fd, &_master_fds, &_max_fds);
	
	return fd;
}


bool CANBusMgr::stop(string ifName, int *errorOut){
	
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
	if(errorOut) *errorOut = ENXIO;
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
	
done:
	if(errorOut) *errorOut = error;
	return false;
}

void CANBusMgr::pauseReading(){
	if(isReadingFile() && !_paused){
		_paused = true;
	}
}

void CANBusMgr::resumeReading(){
	if(isReadingFile() && _paused){
		_paused = false;

	}

}


void CANBusMgr::readFileThread(std::ifstream	*ifs, bool nodelay, voidCallback_t doneCallBack) {
	
//	std::ifstream	ifs;
 	uint32_t number = 0;
	string line;
	
	unsigned long lastTimestamp = 0;

	try{
		while (_reading && std::getline(*ifs, line) ) {
			
			while(_paused){
				usleep(500);
			};
			if(!_reading) break;
			
			number++;
			bool failed = false;
			
			struct can_frame frame;
			struct timeval tv;
			unsigned long timestamp = 0;
			
			const char *p = line.c_str() ;
			int n;
			char canport[20];
			
			int temp;	// multiplatform issue with tv_usec
			 
			if( sscanf(p,"(%ld.%d) %s%x#%n",
						  &tv.tv_sec,
						  &temp,
						  canport,
						  &frame.can_id,
						  &n) != 4) continue;
			
			tv.tv_usec = temp;
	 
			timestamp = (tv.tv_sec * 100 ) + (tv.tv_usec / 10000);
			p = p+n;
			
			frame.can_dlc = 0;
			while(*p) {
				uint8_t b1;
				
				if(sscanf(p, "%02hhx", &b1) != 1){
					failed = true;
					break;
				}
				
				frame.data[frame.can_dlc++] = b1;
				p+=2;
				if(frame.can_dlc > CAN_MAX_DLEN) {
					failed = true;
					break;
				}
			}
			if(!failed){
				_frameDB.saveFrame(string(canport), frame, timestamp);
				
				if(nodelay){
					usleep(500);
				}
				else {
					unsigned long delay = timestamp - lastTimestamp;
					if (delay <= 0)
						usleep(500);
					else
						usleep(delay > 5000 ? 1000000: (int) delay * 10000);
		 
				}
				if(timestamp> lastTimestamp)
					lastTimestamp = timestamp;
				
			}
		}
	}
	catch(std::ifstream::failure &err) {
	//	printf("readFramesFromFile FAIL: %s", err.what());
 	}
	_reading = false;
	ifs->close();
	delete ifs;
		
	if(doneCallBack) doneCallBack();
	
}
	


bool CANBusMgr::readFramesFromFile(string filePath, bool nodelay, int *errorOut, voidCallback_t doneCallBack){
	
	
	// are we already reading a file abort then,
	if(_reading){
		if(errorOut) *errorOut = EBUSY;
		return false;
	}
	
	if(filePath.empty())
		return false;
	
	// start fresh frames
	_frameDB.clearFrames();
	
	bool	statusOk = false;
	try{
		string line;
		std::ifstream	*ifs  = new ifstream;
 
		// open the file
		ifs->open(filePath, ios::in);
		if(!ifs->is_open()) {
			delete ifs;
			if(errorOut) *errorOut = errno;
			return false;
		}
		
		// start thread
		_reading = true;
		_paused = false;
		_thread1 = std::thread(&CANBusMgr::readFileThread,  this, ifs,nodelay, doneCallBack);
 		_thread1.detach();
 
		statusOk = true;
	}
	
	catch(std::ifstream::failure &err) {
		printf("readFramesFromFile FAIL: %s", err.what());
		statusOk = false;
	}
	
	return statusOk;
}


void CANBusMgr::CANThread() {
	
	struct can_frame frame;

	while(_running){
		
		/* wait for something to happen on the socket */
		struct timeval selTimeout;
		selTimeout.tv_sec = 0;       /* timeout (secs.) */
		selTimeout.tv_usec = 100;            /* 100 microseconds */
		
		/* back up master */
		fd_set dup = _master_fds;
		
		int numReady = select(_max_fds+1, &dup, NULL, NULL, &selTimeout);
		if( numReady == -1 ) {
			perror("select");
			_running = false;
		}
		
		/* check which fd is avaialbe for read */
		for (auto& [ifName, fd]  : _interfaces) {
			if ((fd != -1)  && FD_ISSET(fd, &dup)) {
		 
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
