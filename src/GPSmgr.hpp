//
//  GPSmgr.hpp
//  GPStest
//
//  Created by Vincent Moscaritolo on 5/18/22.
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
 
#if USE_SERIAL_GPS
#include <termios.h>
#else
#define UBLOX_CURRENT_ADDRESS_READ 1
#include "I2C.hpp"
#endif

#include <sys/time.h>

 #include "ErrorMgr.hpp"
#include "CommonDefs.hpp"
#include "MicroNMEA.hpp"



using namespace std;
 
typedef double GPSLocationDegrees;    //   degrees
typedef double  GPSLocationDistance;	// distance in meters.

typedef struct {
	GPSLocationDegrees 		latitude;
	GPSLocationDegrees 		longitude;
	GPSLocationDistance	 	altitude;
	time_t						timestamp;	//local timestamp of reading
	
	uint8_t						HDOP;   // dilution of precision (HDOP), in tenths

		/* DOP Value	Rating	Description
		 1			Ideal			This is the highest possible confidence level to be used for applications
		 demanding the highest possible precision at all times.

		 1-2		Excellent	At this confidence level, positional measurements are considered accurate
		 enough to meet all but the most sensitive applications.

		 2-5			Good		Represents a level that marks the minimum appropriate for making business decisions.
		 Positional measurements could be used to make reliable 			in-route navigation suggestions to the user.

		 5-10	Moderate		Positional measurements could be used for calculations, but the fix quality could
		 still be improved. A more open view of the sky is recommended.

		 10-20	Fair			Represents a low confidence level. Positional measurements should be discarded or used only
		 to indicate a very rough estimate of the current location.

		 >20		Poor			At this level, measurements are inaccurate by as much as 300 meters with a 6 meter
		 accurate device (50 DOP × 6 meters) and should be discarded.
		 */

	char 					navSystem;			 // navigation system
	 /*
	  navigation systems
	  `N` = GNSS   		Mix
	  `P` = GPS				US
	  `L` = GLONASS		RU
	  `A` = Galileo		EU
	  `\0` = none
		*/
	
	uint8_t 						numSat;			// number of satellites for fix
	
	GPSLocationDistance		geoidHeight;  //height above WGS84 Geoid
	
	bool isValid;
	bool altitudeIsValid;
	bool geoidHeightValid;
 
} GPSLocation_t;


typedef struct {
	double 					speed;
 	double					heading;
	time_t					timestamp;	//local timestamp of reading
 	bool						 isValid;
} GPSVelocity_t;

class GPSmgr {
	
public:

	GPSmgr();
	~GPSmgr();
	
	
#if USE_SERIAL_GPS
	bool begin(const char* path = "/dev/ttyAMA0", speed_t speed =  B9600);
	bool begin(const char* path, speed_t speed, int &error);
#else
	bool begin(uint8_t deviceAddress = 0x42);
	bool begin(uint8_t deviceAddress,  int &error);
	uint8_t	getDevAddr();
	
	bool setShouldRead(bool shouldRead);
	bool shouldRead() {return _shouldRead;};

#endif

	void stop();

	bool reset();
	bool isConnected() ;

	bool GetLocation(GPSLocation_t& location);
	static string UTMString(GPSLocation_t location);
	static string NavString(char navSystem );
	
	bool GetVelocity(GPSVelocity_t & velocity);
	
	
	
private:
	bool 				_isSetup = false;
	GPSLocation_t	_lastLocation;
	GPSVelocity_t	_lastVelocity;
	
	MicroNMEA		_nmea;
	uint8_t			_nmeaBuffer[128];
  
#if USE_SERIAL_GPS
	const char* 	_ttyPath = NULL;
	speed_t 			_ttySpeed;
	
	bool openGPSPort(int &error);
	void closeGPSPort();
	
	  struct termios _tty_opts_backup;
	  fd_set	 			_master_fds;		// Can sockets that are ready for read
	  int					_max_fds;
	  int	 				_fd;
#else
	I2C 				_i2cPort;
	bool				_shouldRead = false;

#endif
	
	void processNMEA();
 
	void GPSReader();		// C++ version of thread
	// C wrappers for GPSReader;
	static void* GPSReaderThread(void *context);
	static void GPSReaderThreadCleanup(void *context);
	bool 			_isRunning = false;
 
  pthread_cond_t 		_cond = PTHREAD_COND_INITIALIZER;
  pthread_mutex_t 	_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_t				_TID;
};
