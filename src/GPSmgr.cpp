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


GPSmgr::GPSmgr() {
	_isSetup = false;
	
	FD_ZERO(&_master_fds);
	_max_fds = 0;
	
	_ttyPath = NULL;
	_ttySpeed = B0;
	
	_fd = -1;
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
	options.c_cflag &= ~CRTSCTS;            // Disable hardware flow control
	options.c_cflag |= (CREAD | CLOCAL); // Turn on READ & ignore ctrl lines (CLOCAL = 1)

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


/*
* Great-circle distance computational forumlas
*
* https://en.wikipedia.org/wiki/Great-circle_distance
https://www.movable-type.co.uk/scripts/latlong.html
*/

template<typename T, typename U>
constexpr double dmod (T x, U mod)
{
	return !mod ? x : x - mod * static_cast<long long>(x / mod);
}

pair<double,double>  GPSmgr::dist_bearing(GPSLocation_t p1, GPSLocation_t p2){

  constexpr double earth_radius_km = 6371; //6368.519;
  constexpr double PI_360 =  PI / 360;
  constexpr double PI_180 = PI_360 * 2.;

  // spherical law of cosines
	const double cLat =  cos((p1.latitude + p2.latitude) * PI_360);
	const double dLat = (p2.latitude - p1.latitude) * PI_360;
	const double dLon = (p2.longitude - p1.longitude) * PI_360;

  const double f = dLat * dLat + cLat * cLat * dLon * dLon;
  const double c = 2 * atan2(sqrt(f), sqrt(1 - f));
  double dist = earth_radius_km * c;

  // covert to radians
  double lat1 =  p1.latitude * PI_180;
  double lon1 =  p1.longitude * PI_180;
  double lat2 =  p2.latitude * PI_180;
  double lon2 =  p2.longitude * PI_180;
  
  double  b_rad = atan2(sin(lon2-lon1)*cos(lat2), cos(lat1)*sin(lat2)-sin(lat1)*cos(lat2)*cos(lon2-lon1));
  double  b_deg=  b_rad * (180.0 / PI);
  b_deg = dmod ((b_deg + 360.) ,360);

  return std::make_pair(dist, b_deg);
}


// MARK: -  GPSReader thread


// call then when _nmea.process  is true
void GPSmgr::processNMEA(const char *sentence){
	
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now );
	
	struct timespec utc;
	clock_gettime(CLOCK_REALTIME, &utc );
	
	switch (minmea_sentence_id(sentence, false)) {
			
			//Recommended Minimum
		case MINMEA_SENTENCE_RMC: {
			struct minmea_sentence_rmc frame;
			if (minmea_parse_rmc(&frame, sentence)) {
				
				if(frame.valid){
					
					pthread_mutex_lock (&_mutex);
					memset((void*)&_lastVelocity, 0, sizeof(_lastVelocity));
					
					double heading =  minmea_tofloat(&frame.course);
					double speed 	=  minmea_tofloat(&frame.speed);
					
					if(frame.course.scale != 0 && frame.speed.scale != 0){
						_lastVelocity.heading 	= heading;
						_lastVelocity.speed 		= speed;
						_lastVelocity.isValid 	= 	true;
						_lastVelocity.timestamp = now;
					}
					
					struct timespec gpsTime;				// GPS time
					if(minmea_gettime( &gpsTime, &frame.date, &frame.time) == 0){
						_lastGPSTime.gpsTime = gpsTime;
						_lastGPSTime.timestamp = now;
						_lastGPSTime.isValid 	= 	true;
					}
					
					// check against clock */
					time_t diffSecs = abs( _lastGPSTime.gpsTime.tv_sec - utc.tv_sec);
					pthread_mutex_unlock (&_mutex);
					
					// detect clock difference - -tell piCarMgr
					if(diffSecs  > 0){
						
						//						printf("clock is off by %ld secs \n",diffSecs);
						PiCarMgr* mgr 		= PiCarMgr::shared();
						mgr->clockNeedsSync(diffSecs, _lastGPSTime.gpsTime);
					}
				}
				
			}
			break;
			
		case MINMEA_SENTENCE_GGA: {
			struct minmea_sentence_gga frame;
			if (minmea_parse_gga(&frame, sentence)) {
				
				if(frame.fix_quality >=  1 &&  frame.fix_quality <= 5 ) {
					pthread_mutex_lock (&_mutex);
					memset((void*)&_lastLocation, 0, sizeof(_lastLocation));
					
					if(frame.latitude.scale != 0 && frame.longitude.scale != 0){
						double latitude =  minmea_tocoord(&frame.latitude);
						double longitude =  minmea_tocoord(&frame.longitude);
						
						_lastLocation.latitude = latitude;
						_lastLocation.longitude = longitude;
						_lastLocation.isValid = true;
					}
					
					if(  frame.altitude.scale != 0 && frame.altitude_units == 'M'){
						double altitude =  minmea_tofloat(&frame.altitude);
						_lastLocation.altitude = altitude  ; // tenths of meter
						_lastLocation.altitudeIsValid = true;
						
						if(  frame.height.scale != 0 && frame.height_units == 'M'){
							double geoidHeight =  minmea_tofloat(&frame.height);
							_lastLocation.geoidHeight = geoidHeight * .1;
							_lastLocation.geoidHeightValid = true;
						}
					}
					
					if(  frame.hdop.scale != 0 ){
						double hdop =  minmea_tofloat(&frame.hdop);
						_lastLocation.HDOP = int(hdop * 10);
					}
					
					if(sentence[1] == 'G') {
						_lastLocation.navSystem = sentence[2];
					}
					
					_lastLocation.numSat = frame.satellites_tracked;
					_lastLocation.timestamp = utc;
					pthread_mutex_unlock (&_mutex);
				}
			}
		}
			break;
			
		default:
			break;
			
		}
	}
	
}
// MARK: -

void GPSmgr::GPSReader(){
	
	PRINT_CLASS_TID;

	char	 buffer[82] = {0};
	char	 *p = buffer;
	
	while(_isRunning){
		
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
		selTimeout.tv_usec = 100;            /* 100 microseconds */
		
		/* back up master */
		fd_set dup = _master_fds;
		
		int numReady = select(_max_fds+1, &dup, NULL, NULL, &selTimeout);
		if( numReady == -1 ) {
			perror("select");
		}
		
		if ((_fd != -1)  && FD_ISSET(_fd, &dup)) {
			
			size_t avail = sizeof(buffer) - (p - buffer);
			// overflow   do reset
			if(avail == 0){
				p = buffer;
				*p = 0;
				continue;
			}
			
			u_int8_t c;
			size_t nbytes =  (size_t)::read( _fd, &c, 1 );
			
			if(nbytes == 1){
				
				if(c == '\r') continue;
				
				if (c == 0 || c == '\n'){
					*p = 0;
//					printf("%s\n", buffer);
					processNMEA(buffer);
					p = buffer;
					*p = 0;
				}else {
					*p++ = c;
					
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
				}
				
				else {
					perror("read");
				}
			}
		}
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

