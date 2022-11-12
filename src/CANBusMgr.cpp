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
#include "XXHash32.h"

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
	_frame_handlers.clear();

	_lastPollTime = {0,0};
	_pollDelay = 500 ; //  500 milliseconds
 
	_obd_requests = {};
	_obd_polling = {};
	_waiting_isotp_packets = {};
	
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

// MARK: -  ISOTP Handlers
 
 bool CANBusMgr::registerISOTPHandler(string ifName, canid_t can_id,  ISOTPHandlerCB_t cb, void* context){
	
	for( auto item: _frame_handlers){
		if(item.ifName == ifName
			&& item.can_id == can_id
			&& item.context == context)
			return false;
	}
 
	 frame_handler_t handler = {
		.ifName = ifName,
		.can_id = can_id,
		.cb = cb,
		.context = context
	};
	
	 _frame_handlers.push_back(handler);
	
	return true;
}

void CANBusMgr::unRegisterISOTPHandler(string ifName, canid_t can_id, ISOTPHandlerCB_t cb ){
	
	_frame_handlers.erase(
		 std::remove_if(_frame_handlers.begin(), _frame_handlers.end(),
							 [](const frame_handler_t & item) { return false; }),
								 _frame_handlers.end());
}

bool CANBusMgr::shouldProcessIOSTPforCanID(string ifName, canid_t can_id){
	
	bool hasHandler = false;
	
	for( auto item: _frame_handlers)
		if(item.ifName == ifName
			&& item.can_id == can_id ){
			
			hasHandler = true;
			break;
		}
	
	
	if(!hasHandler){
		for( auto & [key,val]: _waiting_isotp_packets)
			if(val.reply_id == can_id){
				hasHandler = true;
				break;
			}
 	}
 
	return hasHandler;
}
 
void CANBusMgr::processISOTPFrame(string ifName, can_frame_t frame, unsigned long  timeStamp){
	
	// are there any handlers for this canID
	canid_t can_id = frame.can_id & CAN_ERR_MASK;
	if(! shouldProcessIOSTPforCanID(ifName, can_id ))  return;
	
 	uint8_t frame_type = frame.data[0]>> 4;
	
	// is it a single frame request
	if(frame_type == 0){
 
		uint8_t len = frame.data[0] & 0x07;
		uint8_t* data = &frame.data[1];
		//		bool REQ = (data[0] & 0x40)  == 0 ;
		//
		//		if(REQ ){
		vector<uint8_t> bytes;
		bytes.reserve(len);
		std::copy(data, data + len, std::back_inserter(bytes));
//		
//		{
//			printf("rcv  %03x [%2d] ", can_id, (int) bytes.size() );
//			for(int i = 0; i < bytes.size(); i++) printf("%02x ", bytes[i]);
//			printf("|\n");
//		}
//		
		for(auto d : _frame_handlers)
			if(d.ifName == ifName
				&& d.can_id == can_id ){
			
			ISOTPHandlerCB_t	cb = d.cb;
			void* context 			= d.context;
			
			if(cb) (cb)(context,ifName, can_id, bytes, timeStamp);
		}
	}
	else if(frame_type == 3){
		//  flow control C  frame
	 
		uint8_t frame_flag 		= frame.data[0] & 0x0f;
		uint8_t block_size 		= frame.data[1];
		uint8_t separation_delay = frame.data[2];

		uint32_t hash = XXHash32::hash(ifName + to_hex(can_id, true));
		
		if(_waiting_isotp_packets.count(hash)){
			auto &s = _waiting_isotp_packets[hash];
			
			 // hash sanity check
			if(! (s.reply_id == can_id && s.ifName == ifName))
				return;
			
			
			// update the separation_delay
			s.separation_delay = separation_delay;
			clock_gettime(CLOCK_MONOTONIC, &s.lastSentTime);
		
			
	#warning -- handle separation_delay

			if(frame_flag == 0) {
				// force all output for now
				
				uint8_t cnt = 0;
				for( uint16_t offset = s.bytes_sent; offset < s.bytes.size(); offset+= 7){
				
					vector<uint8_t> data;
					data.reserve(8);
					data.push_back(static_cast<uint8_t> ( 0x20 | ( cnt++ & 0x0f)));
					
					for(auto i = 0; i < 7; i++){
						if((offset + i) >  s.bytes.size()) break;
						data.push_back(s.bytes[offset + i]);
					}
					sendFrame(s.ifName,s.can_id, data);
				}
			
 				_waiting_isotp_packets.erase(hash);
			}
			
		}
 
	}
	
}


   //
 //#warning write code to process multi frame
 //	/*
 //	 The initial byte contains the type (type = 3) in the first four bits,
 //	 and a flag in the next four bits indicating if the transfer is allowed
 //	 (0 = Clear To Send,
 //	 1 = Wait,
 //	 2 = Overflow/abort).
 //	 The next byte is the block size, the count of frames that may be sent before waiting for the next flow control frame.
 //	 A value of zero allows the remaining frames to be sent without flow control or delay.
 //
 //	 The third byte is the Separation Time (ST), the minimum delay time between frames.
 //	 ST values up to 127 (0x7F) specify the minimum number of milliseconds to delay between frames,
 //	 while values in the range 241 (0xF1) to 249 (0xF9) specify delays increasing from 100 to 900 microseconds.
 //
 //	 Note that the Separation Time is defined as the minimum time between the end of one frame to the beginning of the next.
 //	 Robust implementations should be prepared to accept frames from a sender that misinterprets this as the
 //	 frame repetition rate i.e. from start-of-frame to start-of-frame.
 //	 Even careful implementations may fail to account for the minor effect of bit-stuffing in the physical layer.
 //
 //	The sender transmits the rest of the message using Consecutive Frames.
 //	 Each Consecutive Frame has a one byte PCI, with a four bit type (type = 2) followed by a 4-bit sequence number.
 //	 The sequence number starts at 1 and increments with each frame sent (1, 2,..., 15, 0, 1,...),
 //	 with which lost or discarded frames can be detected.
 //
 //	 Each consecutive frame starts at 0, initially for the first set of data in the first frame will be considered as 0th data.
 //	 So the first set of CF(Consecutive frames) start from "1".
 //	 There afterwards when it reaches "15", will be started from "0".
 //	 The 12 bit length field (in the FF) allows up to 4095 bytes of user data in a segmented message,
 //	 but in practice the typical application-specific limit is considerably lower because of receive buffer or hardware limitations.
 //	 */
 //
 //}
 


bool CANBusMgr::sendISOTP(string ifName, canid_t can_id, canid_t reply_id,  vector<uint8_t> bytes,  int* error){
	bool success = false;
	
	uint len = (uint)bytes.size();

	if (len > 4096)
		throw Exception("sendISOTP packet too long");

// debug
	{
		printf("send  %03x [%2d] ", can_id, (int) len );
		for(int i = 0; i < len; i++) printf("%02x ", bytes[i]);
		printf("|\n");
	}
	
	if(len < 8){
		
		// add length in front and create single frame.
		vector<uint8_t> data;
		data.reserve(bytes.size() + 1);
 		data.push_back(static_cast<uint8_t> ( len & 0x0f));
		data.insert(data.end(), bytes.begin(), bytes.end());
 		sendFrame(ifName,can_id, data);
 	}
	else {
		// multi frame
		std::lock_guard<std::mutex> lock(_isotp_mutex);

		// create a new state
		isotp_state_t s;
		
		s.ifName = ifName;
		s.can_id	= can_id;
		s.reply_id = reply_id;
		s.bytes = bytes;
		s.bytes_sent = 6;
		s.separation_delay = 0;
 		clock_gettime(CLOCK_MONOTONIC, &s.lastSentTime);
 		uint32_t hash = XXHash32::hash(ifName +  to_hex(reply_id, true)  );
		
 		_waiting_isotp_packets[hash] = s;
	 
		// send first packet
		vector<uint8_t> data;
		data.reserve(8);

		// create first packet   | 0001 | Len11 - Len8 | Len7 - Len0 |
		data.push_back(static_cast<uint8_t> ( 0x10 | ((len >> 8) & 0x0f)));
		data.push_back(static_cast<uint8_t> ( len & 0xff));
		// send first 6 bytes
		for(int i = 0; i < 6; i++){
			data.push_back(bytes[i]);
		}

		sendFrame(ifName,can_id, data);
 
/* debug with
	candump can0,6b0:7ff,516:7ff -a
 
 
 cansend can0 6B0#023e010000000000
 cansend can0 6B0#041800FF00000000
 cansend can0 6B0#021A870000000000
 cansend can0 6B0#0221E10000000000
*/
	
	}
	return success;
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
 
					frame.can_dlc = 8 ;  // always send 8 bytes  frames.   bytes.size();
					
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




bool CANBusMgr::lastFrameTime(string ifName, time_t &timeOut){
	
	time_t lastTime = 0;
	
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

// MARK: - periodic tasks


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
		diff = timespec_sub(now, _lastPollTime);
		if(timespec_to_ms(diff) >= _pollDelay){
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


bool CANBusMgr::setPeriodicCallback (string ifName, int64_t delay,
												 periodicCallBackID_t & callBackID,
												 void* context,
												 periodicCallBack_t cb  ){
 
	std::uniform_int_distribution<periodicCallBackID_t> distribution(0,UINT32_MAX);

	periodic_task_t newTask;
	
	newTask.taskID =  distribution(_rng);
	newTask.ifName = ifName;
	newTask.delay = delay;
	newTask.cb	= cb;
	newTask.context = context;
	newTask.lastRun  = {0,0};
	_periodic_tasks[newTask.taskID] = newTask;
	
//	printf("setPeriodicCallback %08x\n", newTask.taskID);

	return true;
 }

bool CANBusMgr::removePeriodicCallback (periodicCallBackID_t callBackID ){
	 
	if( _periodic_tasks.count(callBackID)){
		
//		printf("removePeriodicCallback %08x\n", callBackID);
 		_periodic_tasks.erase(callBackID);
		return true;
	}
	return false;
}


void CANBusMgr::processPeriodicRequests(){
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	
	for (auto& [key, task]  : _periodic_tasks){
		
		bool shouldRun = false;
		
 		struct timespec  diff = timespec_sub(now, task.lastRun);
		if(timespec_to_ms(diff) >= task.delay){
			shouldRun = true;
 		}

		if(shouldRun){
			
			auto cb = task.cb;
	 		if(cb){
				vector<uint8_t>  bytes;
				canid_t can_id;
				
 				if( (cb)(task.context, can_id, bytes)){
	
//					printf("send Frame %03x to %s\n", can_id, task.ifName.c_str());

					int error = 0;
 					if(!sendFrame(task.ifName, can_id, bytes, &error)){
						// send failed
					};
					}
			}
				 
			task.lastRun = now;
		}
	}
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
		
		// we use a timeout so we can end this thread when _isSetup is false
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
		diff = timespec_sub(now, lastTime);
		int64_t timestamp_secs = timespec_to_ms(now) /1000;
		
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
					_lastFrameTime[ifName] =  timestamp_secs;
					_totalPacketCount[ifName]++;
					_runningPacketCount[ifName]++;
 
					// give handlers a crack at the frame
					processISOTPFrame(ifName, frame, timestamp_secs);
				}
			}
		}
		
		// did more than a second go by
		if(timespec_to_ms(diff) > 1000){
			lastTime = now;
	 
			// calulate avareage
			for (auto& [ifName, fd]  : _interfaces) {
				_avgPacketsPerSecond[ifName] = 	(_runningPacketCount[ifName] + _avgPacketsPerSecond[ifName] ) / 2;
				_runningPacketCount[ifName]  = 0;
			}
 		}
 
		// process any needed OBD requests 
		processOBDrequests();
		processPeriodicRequests();
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
 
