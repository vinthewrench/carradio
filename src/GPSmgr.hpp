//
//  GPSmgr.hpp
//  GPStest
//
//  Created by Vincent Moscaritolo on 5/18/22.
//

#pragma once

#include <stdio.h>
#include <pthread.h>


#include <mutex>
#include <utility>      // std::pair, std::make_pair
#include <string>       // std::string
#include <queue>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>
 
#include <sys/time.h>

 #include "ErrorMgr.hpp"
#include "CommonDefs.hpp"
#include "MicroNMEA.hpp"

using namespace std;


class GPSmgr {
	
public:

	GPSmgr();
	~GPSmgr();
		
	bool begin(const char* path = "/dev/ttyAMA0", speed_t speed =  B9600);
	bool begin(const char* path, speed_t speed, int &error);
	void stop();

	 bool reset();

private:
	bool 				_isSetup = false;
 
	MicroNMEA		_nmea;
	uint8_t			_nmeaBuffer[128];
 
	void processNMEA();
	
	void GPSReader();		// C++ version of thread
	// C wrappers for GPSReader;
	static void* GPSReaderThread(void *context);
	static void GPSReaderThreadCleanup(void *context);
	bool 			_isRunning = false;
 
	struct termios _tty_opts_backup;
	fd_set	 			_master_fds;		// Can sockets that are ready for read
	int					_max_fds;
	int	 				_fd;

  pthread_cond_t 		_cond = PTHREAD_COND_INITIALIZER;
  pthread_mutex_t 	_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_t				_TID;

};
