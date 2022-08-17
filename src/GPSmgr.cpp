//
//  GPSmgr.cpp
//  GPStest
//
//  Created by Vincent Moscaritolo on 5/18/22.
//

#include "GPSmgr.hpp"
#include "PiCarMgr.hpp"

#include <fcntl.h>
#include <cassert>
#include <string.h>

#include <stdlib.h>
#include <errno.h> // Error integer and strerror() function
#include "ErrorMgr.hpp"
#include "utm.hpp"
#include "timespec_util.h"
#include <time.h>

#ifndef PI
#define PI           3.14159265358979323e0    /* PI                        */
#endif


typedef void * (*THREADFUNCPTR)(void *);

// MARK: -  SERIAL GPS
#if USE_SERIAL_GPS
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


GPSmgr::GPSmgr() : _nmea( (void*)_nmeaBuffer, sizeof(_nmeaBuffer), this ){
	_isSetup = false;
 
	FD_ZERO(&_master_fds);
	_max_fds = 0;
	
	_ttyPath = NULL;
	_ttySpeed = B0;

	_fd = -1;
//	_nmea.setBuffer( (void*)_nmeaBuffer, sizeof(_nmeaBuffer));
	_nmea.clear();
	
	_isRunning = true;

	pthread_create(&_TID, NULL,
										  (THREADFUNCPTR) &GPSmgr::GPSReaderThread, (void*)this);

	
}

GPSmgr::~GPSmgr(){
	stop();
	
	pthread_mutex_lock (&_mutex);
	_isRunning = false;
	pthread_cond_signal(&_cond);
	pthread_mutex_unlock (&_mutex);

	pthread_join(_TID, NULL);

	FD_ZERO(&_master_fds);
	_max_fds = 0;

}



bool GPSmgr::begin(const char* path, speed_t speed){
	int error = 0;
	
	return begin(path, speed, error);
}


bool GPSmgr::begin(const char* path, speed_t speed,  int &error){

	if(isConnected())
		return true;
	
	_isSetup = false;
	
	if(_ttyPath){
		free((void*) _ttyPath); _ttyPath = NULL;
	}

	pthread_mutex_lock (&_mutex);
	_ttyPath = strdup(path);
	_ttySpeed = speed;
	pthread_mutex_unlock (&_mutex);

	reset();
	_nmea.clear();
	 
	{
		int ignoreError;
		openGPSPort(ignoreError);
 	}
 
	_isSetup = true;

	return _isSetup;
}

bool GPSmgr::openGPSPort( int &error){

	if(!_ttyPath  || _ttySpeed == B0) {
		error = EINVAL;
		return false;
	}
	
	struct termios options;
	
	int fd ;
	
	if((fd = ::open( _ttyPath, O_RDWR | O_NOCTTY | O_NONBLOCK | O_NDELAY  )) <0) {
	//	ELOG_ERROR(ErrorMgr::FAC_GPS, 0, errno, "OPEN %s", _ttyPath);
		error = errno;
		return false;
	}
	
	fcntl(fd, F_SETFL, 0);      // Clear the file status flags
	
	// Back up current TTY settings
	if( tcgetattr(fd, &_tty_opts_backup)<0) {
		ELOG_ERROR(ErrorMgr::FAC_GPS, 0, errno, "tcgetattr %s", _ttyPath);
		error = errno;
		return false;
	}
	
	cfmakeraw(&options);
	options.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
	options.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
	options.c_cflag &= ~CSIZE; // Clear all bits that set the data size
	options.c_cflag |= CS8; // 8 bits per byte (most common)
	options.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control 	options.c_cflag |=  CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
	//options.c_cflag |=  CRTSCTS; // DCTS flow control of output
	options.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)
	
	options.c_lflag &= ~ICANON;
	options.c_lflag &= ~ECHO; // Disable echo
	options.c_lflag &= ~ECHOE; // Disable erasure
	options.c_lflag &= ~ECHONL; // Disable new-line echo
	options.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
	options.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
	options.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes
	
	options.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
	options.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed
	
	cfsetospeed (&options, _ttySpeed);
	cfsetispeed (&options, _ttySpeed);
	
	if (tcsetattr(fd, TCSANOW, &options) < 0){
		ELOG_ERROR(ErrorMgr::FAC_GPS, 0, errno, "Unable to tcsetattr %s", _ttyPath);
		error = errno;
		return false;
	}
	
	pthread_mutex_lock (&_mutex);
	_fd = fd;
	// add to read set
	safe_fd_set(_fd, &_master_fds, &_max_fds);
	pthread_mutex_unlock (&_mutex);

	return true;
}

void GPSmgr::closeGPSPort(){
	if(isConnected()){
		
		pthread_mutex_lock (&_mutex);

		// Restore previous TTY settings
		tcsetattr(_fd, TCSANOW, &_tty_opts_backup);
		close(_fd);
		safe_fd_clr(_fd, &_master_fds, &_max_fds);
		_fd = -1;
		pthread_mutex_unlock (&_mutex);
	}
}

bool  GPSmgr::isConnected() {
	bool val = false;
	
	pthread_mutex_lock (&_mutex);
	val = _fd != -1;
	pthread_mutex_unlock (&_mutex);
 
	return val;
};

void GPSmgr::stop(){
	
	if(_isSetup) {
		closeGPSPort();
		_isSetup = false;
		}
}

#else
// MARK: -  I2C GPS
enum UBLOX_Register
{
  UBLOX_BYTES_AVAIL  = 0xFD,
  UBLOX_DATA_STREAM = 0xFF,
};


GPSmgr::GPSmgr() : _nmea( (void*)_nmeaBuffer, sizeof(_nmeaBuffer), this ){
	_isSetup = false;
	_shouldRead = false;

	_nmea.clear();
	
	_isRunning = true;

	pthread_create(&_TID, NULL,
										  (THREADFUNCPTR) &GPSmgr::GPSReaderThread, (void*)this);

	
}

GPSmgr::~GPSmgr(){
	stop();
	
	pthread_mutex_lock (&_mutex);
	_isRunning = false;
	_shouldRead = false;

	pthread_cond_signal(&_cond);
	pthread_mutex_unlock (&_mutex);
	pthread_join(_TID, NULL);
 }


bool GPSmgr::begin(uint8_t deviceAddress){
	int error = 0;

	return begin(deviceAddress, error);
}
 
bool GPSmgr::begin(uint8_t deviceAddress,   int &error){
	
	reset();
	_nmea.clear();
	_shouldRead = false;

	static const char *ic2_device = "/dev/i2c-22";

	if(  _i2cPort.begin(deviceAddress,ic2_device, error) ){
			_isSetup = true;
	}
	
	return _isSetup;
}
 
void GPSmgr::stop(){
	_isSetup = false;
	reset();
	_nmea.clear();

	_i2cPort.stop();
}

uint8_t	GPSmgr::getDevAddr(){
	return _i2cPort.getDevAddr();
};

 

bool  GPSmgr::isConnected() {
	return _isSetup;
};

bool GPSmgr::setShouldRead(bool shouldRead){
	if(_isSetup && _isRunning){
		_shouldRead = shouldRead;
		return true;
	}
	return false;
}


#endif

// MARK: -

bool GPSmgr::reset(){

	pthread_mutex_lock (&_mutex);
	_lastLocation.isValid = false;
	_lastLocation.altitude = false;
	_lastLocation.HDOP = 255;
	pthread_mutex_unlock (&_mutex);

	return true;
}
 
bool	GPSmgr::GetLocation(GPSLocation_t & location){
 
	bool success = false;
	pthread_mutex_lock (&_mutex);
	if(_lastLocation.isValid ){
		location = _lastLocation;
		success = true;
	}
	pthread_mutex_unlock (&_mutex);
	return success;
}


bool GPSmgr::GetVelocity(GPSVelocity_t& velocity){
	bool success = false;
	pthread_mutex_lock (&_mutex);
 
	if(_lastVelocity.isValid ){
		velocity = _lastVelocity;
		success = true;
	}
 
	pthread_mutex_unlock (&_mutex);
	return success;
}


// MARK: -  Utilities
 

string GPSmgr::UTMString(GPSLocation_t location){
	string str = string();
	
	if(location.isValid){
		long  Zone;
		char 	latBand;
		char  Hemisphere;
		double Easting;
		double Northing;
	
		double latRad = (PI/180.) * location.latitude;
		double lonRad = (PI/180.) * location.longitude;
	
		if( Convert_Geodetic_To_UTM(latRad, lonRad,
											 &Zone,&latBand, &Hemisphere, &Easting, &Northing ) == UTM_NO_ERROR){
			
			char utmBuffer[32] = {0};
			sprintf(utmBuffer,  "%d%c %ld %ld", (int)Zone, latBand, (long) Easting, (long) Northing);
			str = string(utmBuffer);
		}
	}
	
	return str;
	
}

string GPSmgr::NavString(char navSystem ){
	string str = string();
	switch(navSystem){
		case 'N' : str = "GNSS"; break;
		case 'P' : str = "GPS"; break;
		case 'L' : str = "GLONASS"; break;
		case 'A' : str = "Galileo"; break;
		default: break;
	}
	
	return str;
}

// MARK: -  GPSReader thread


// call then when _nmea.process  is true
void GPSmgr::processNMEA(){
	
	string msgID =  string(_nmea.getMessageID());
	
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now );
	
	struct timespec utc;
	clock_gettime(CLOCK_REALTIME, &utc );
	
	//  GGA	Global Positioning System Fix Data
	if( msgID ==  "GGA") {
		//Global Positioning System Fix Data
		
		{
			long  tmp;
			
			pthread_mutex_lock (&_mutex);
			memset((void*)&_lastLocation, 0, sizeof(_lastLocation));
			_lastLocation.isValid = _nmea.isValid();
			_lastLocation.latitude = _nmea.getLatitude() / 1e6;
			_lastLocation.longitude = _nmea.getLongitude() / 1e6;
			_lastLocation.altitudeIsValid = _nmea.getAltitude(tmp);
			_lastLocation.altitude 		= tmp * 0.001;
			_lastLocation.navSystem 	= _nmea.getNavSystem();
			_lastLocation.HDOP 			= _nmea.getHDOP();
			_lastLocation.numSat 		= _nmea.getNumSatellites();
			_lastLocation.geoidHeightValid  = _nmea.getGeoidHeight(tmp);
			_lastLocation.geoidHeight 		= tmp * 0.001;
			_lastLocation.timestamp = now;
			pthread_mutex_unlock (&_mutex);
			
		}
		
	}
	else 	if( msgID ==  "RMC") {
		//Recommended Minimum
		
		pthread_mutex_lock (&_mutex);
		memset((void*)&_lastVelocity, 0, sizeof(_lastVelocity));
		_lastVelocity.isValid = _nmea.isValid();
//		_lastVelocity.heading = _nmea.getCourse()/1000.;
//		_lastVelocity.speed = _nmea.getSpeed();
		
		_lastVelocity.heading = 90.0;
		_lastVelocity.speed =  160.93 / 2.;

		_lastVelocity.timestamp = now;
		
		_lastGPSTime.gpsTime = _nmea.getGPStime();
		_lastGPSTime.timestamp = now;
		_lastGPSTime.isValid = true;
		
		// check against clock */
		time_t diffSecs = abs( _lastGPSTime.gpsTime.tv_sec - utc.tv_sec);
		pthread_mutex_unlock (&_mutex);
		
		// detect clock difference - -tell piCarMgr
		if(diffSecs  > 0){
			PiCarMgr* mgr 		= PiCarMgr::shared();
			mgr->clockNeedsSync(diffSecs, _lastGPSTime.gpsTime);
 		}
 	}
}


static void  UnknownSentenceHandler(MicroNMEA & nmea, void *context){
//	GPSmgr* d = (GPSmgr*)context;
	
//	printf("UNKN |%s|\n", nmea.getSentence());
 
	/*
	 
	 */
};


void GPSmgr::GPSReader(){
	 
	_nmea.setUnknownSentenceHandler(UnknownSentenceHandler);
		
	while(_isRunning){
		
		
#if USE_SERIAL_GPS
		// if not setup // check back later
		if(!_isSetup){
			sleep(2);
			continue;
		}

		int lastError = 0;

		// is the port setup yet?
		if (! isConnected()){
			if(!openGPSPort(lastError)){
				sleep(5);
				continue;
			}
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
		}
		
		if ((_fd != -1)  && FD_ISSET(_fd, &dup)) {
			
			bool readMore = false;
			
			do{
		 		readMore = false;
				
				u_int8_t c;
				size_t nbytes =  (size_t)::read( _fd, &c, 1 );
				
				if(nbytes == 1){
					readMore = true;
					if(_nmea.process(c)){
						processNMEA();
					}
				}
				else if( nbytes == 0) {
					continue;
				}
				else if( nbytes == -1) {
					int lastError = errno;
					
					// no data try later
					if(lastError == EAGAIN)
						continue;
					
					if(lastError == ENXIO){  // device disconnected..
						
						ELOG_ERROR(ErrorMgr::FAC_GPS, 0, errno, "GPS disconnectd", _ttyPath);
						
						closeGPSPort();
						continue;
					}
					
					else {
						perror("read");
					}
				}
			} while (readMore);
			
		}
		
#else
		
		// if not setup // check back later
		if(!_shouldRead ){
			usleep(500000);
			continue;
		}

#if UBLOX_CURRENT_ADDRESS_READ
		uint8_t b;
		
		if(_i2cPort.readByte(b)){
			if(b == 0xff){
				// not ready.. wait a bit
				usleep(10000);
			}
			else {
				if(_nmea.process(b)){
					processNMEA();
				}
			}
		}
		else {
			ELOG_ERROR(ErrorMgr::FAC_GPS, 0, errno, "GPS I2C READ FAILED");
			_shouldRead = false;
		}

#else
 
		
		uint16_t len = 0;
		if(_i2cPort.readWord(UBLOX_BYTES_AVAIL, len)
			&& (len > 0) && (len != 0xffff)){
			
			for(uint16_t i = 0; i < len; i++){
				uint8_t b;
				
				if(i == 0){
					if(! _i2cPort.readByte(UBLOX_DATA_STREAM, b)) break;
				}
				else {
					if(! _i2cPort.readByte(b)) break;
				}
				
				if(_nmea.process(b)){
					processNMEA();
				}
			}
			
		}
		else {
			usleep(1000);
		}
		
#endif
#endif
		
	}
}



void* GPSmgr::GPSReaderThread(void *context){
	GPSmgr* d = (GPSmgr*)context;

	//   the pthread_cleanup_push needs to be balanced with pthread_cleanup_pop
	pthread_cleanup_push(   &GPSmgr::GPSReaderThreadCleanup ,context);
 
	d->GPSReader();
	
	pthread_exit(NULL);
	
	pthread_cleanup_pop(0);
	return((void *)1);
}

 
void GPSmgr::GPSReaderThreadCleanup(void *context){
	//GPSmgr* d = (GPSmgr*)context;
 
	printf("cleanup GPSReader\n");
}
 
