//
//  GPSmgr.cpp
//  GPStest
//
//  Created by Vincent Moscaritolo on 5/18/22.
//


#include "GPSmgr.hpp"
#include <fcntl.h>
#include <cassert>
#include <string.h>

#include <stdlib.h>
#include <errno.h> // Error integer and strerror() function
#include "ErrorMgr.hpp"
#include "utm.hpp"
#include <cmath>
typedef void * (*THREADFUNCPTR)(void *);

#ifndef PI
#define PI           3.1415926535897932384626433832795    /* PI                        */
#endif

#ifndef TAU
#define TAU           6.283185307179586476925286766559  /* TAU                        */
#endif


// MARK: -  ubx_checksum

 void UBX_checksum::add(uint8_t c){
	 _CK_A += c;
	 _CK_B += _CK_A;
	 
}

bool UBX_checksum::validate(uint8_t A, uint8_t B){
	
	return (A == _CK_A && B == _CK_B);
  }

 
// MARK: -  Dbuf

#define ALLOC_QUANTUM 16


dbuf::dbuf(){
	_used = 0;
	_pos = 0;
	_alloc = ALLOC_QUANTUM;
	_data =  (uint8_t*) malloc(ALLOC_QUANTUM);
 }

dbuf::~dbuf(){
	if(_data)
		free(_data);
	_data = NULL;
	_used = 0;
	_pos = 0;
	_alloc = 0;
 }
bool dbuf::append_data(void* data, size_t len){
	
	if(len + _pos  >  _alloc){
		size_t newSize = len + _used + ALLOC_QUANTUM;
		
		if( (_data = (uint8_t*) realloc(_data,newSize)) == NULL)
			return false;
		
		_alloc = newSize;
	}
	
	 if(_pos < _used) {
		 memmove((void*) (_data + _pos + len) ,
					(_data + _pos),
					_used -_pos);
	 }
	 
	memcpy((void*) (_data + _pos), data, len);
	 
	 _pos += len;
	 _used += len;
 //	if(_pos > _used)
 //		_used = _pos;
	 
	return true;
}


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
		printf("Error %d, %s\n", errno, strerror(errno) );
		
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
	_lastLocation.DOP = 255;
	_shouldSetLocalTime = false;
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
 

string GPSmgr::headingStringFromHeading(double	 heading){
 
	string ordinal[] =  {"N ","NE","E ", "SE","S ","SW","W ","NW"} ;
	string str = ordinal[int(floor((int(heading) / 45) + 0.5)) % 8]  ;
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

// MARK: -  UBX decode
 
#define DEBUG_UBX 1

// little endian copy
#define TO_U32(_buf_,_offset_)  *((uint32_t*) &((u_int8_t*)_buf_)[_offset_])
#define TO_I32(_buf_,_offset_)  *((int32_t*) &((u_int8_t*)_buf_)[_offset_])
#define TO_U16(_buf_,_offset_)  *((uint16_t*) &((u_int8_t*)_buf_)[_offset_])
#define TO_U8(_buf_,_offset_)  *((uint8_t*) &((u_int8_t*)_buf_)[_offset_])


#if DEBUG_UBX

// for debugging
static void dumpHex(uint8_t* buffer, size_t length, int offset)
{
	char hexDigit[] = "0123456789ABCDEF";
	size_t			i;
	size_t						lineStart;
	size_t						lineLength;
	short					c;
	const unsigned char	  *bufferPtr = buffer;
	
	char                    lineBuf[1024];
	char                    *p;
	 
#define kLineSize	8
	for (lineStart = 0, p = lineBuf; lineStart < length; lineStart += lineLength,  p = lineBuf )
	{
		 lineLength = kLineSize;
		 if (lineStart + lineLength > length)
			  lineLength = length - lineStart;
		 
		p += sprintf(p, "%6lu: ", lineStart+offset);
		 for (i = 0; i < lineLength; i++){
			  *p++ = hexDigit[ bufferPtr[lineStart+i] >>4];
			  *p++ = hexDigit[ bufferPtr[lineStart+i] &0xF];
			  if((lineStart+i) &0x01)  *p++ = ' ';  ;
		 }
		 for (; i < kLineSize; i++)
			  p += sprintf(p, "   ");
		 
		 p += sprintf(p,"  ");
		 for (i = 0; i < lineLength; i++) {
			  c = bufferPtr[lineStart + i] & 0xFF;
			  if (c > ' ' && c < '~')
					*p++ = c ;
			  else {
					*p++ = '.';
			  }
		 }
		 *p++ = 0;
		 
  
		printf("%s\n",lineBuf);
	}
#undef kLineSize
}

#else
#define dumpHex(_arg1_ ,_arg2_ ,_arg3_ )
#endif

void GPSmgr::processUBX(u_int8_t ubx_class, u_int8_t ubx_id,
								u_int8_t *buffer, size_t length){
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now );
	
	struct timespec utc;
	clock_gettime(CLOCK_REALTIME, &utc );

	if( ubx_class == 0x01) {
 
		switch (ubx_id) {
				
			case 0x07:		 //	UBX-NAV-PVT (0x01 0x07)
				if(length == 92) {
// 					printf(" process UBX-NAV-PVT (0x01 0x07)\n");

					bool gotTime = false;
		//				uint32_t iTOW  	= TO_U32(buffer, 0);
					uint16_t year 		= TO_U16(buffer,4);
					uint8_t month 		= TO_U8(buffer,6);
					uint8_t day 		= TO_U8(buffer,7);
					uint8_t hours 		= TO_U8(buffer,8);
					uint8_t minutes 	= TO_U8(buffer,9);
					uint8_t seconds 	= TO_U8(buffer,10);
					uint8_t timeFlags = TO_U8(buffer,11);
					uint8_t flags 		= TO_U8(buffer,21);
		
					uint8_t numSV 		= TO_U8(buffer,23);  // Number of satellites used in Nav Solution
					
					long lon 			= TO_I32(buffer,24);
					long lat				= TO_I32(buffer,28);
					long hMSL 			= TO_I32(buffer,36);
					uint8_t fixType 	= TO_U8(buffer,20);
					uint8_t pDOP 		= TO_U8(buffer,76);
					
					long gSpeed 		= TO_I32(buffer,60);  // Ground Speed (2-D)
					long headMot 		= TO_I32(buffer,64); //Heading of motion (2-D)
	//				uint32_t sAcc 		= TO_I32(buffer,68); //Speed accuracy estimate
	//				uint32_t headAcc 	= TO_I32(buffer,72); //Heading accuracy estimate (both motion and vehicle)
	//				long headVehic 	= TO_I32(buffer,84); //Heading of vehicle (2-D),
				
					
	//				printf("  knots: %f head: %f  \n",   gSpeed  * 0.0019438444924406 , headMot * 1e-5);
	//
	//				printf(" sAcc: %d headAcc: %f speed: %ld head: %f \n", sAcc, headAcc * 1e-5 , gSpeed, headMot * 1e-5);
//
					pthread_mutex_lock (&_mutex);
					
					if((timeFlags & 0x7) == 0x7) {
						struct tm tm;
						memset(&tm, 0, sizeof(tm));
						
						if (year < 80) {
							tm.tm_year = 2000 + year - 1900; // 2000-2079
						} else if (year >= 1900) {
							tm.tm_year = year - 1900;        // 4 digit year, use directly
						} else {
							tm.tm_year = year;               // 1980-1999
						}
						
						tm.tm_mon = month - 1;
						tm.tm_mday = day;
						tm.tm_hour = hours;
						tm.tm_min = minutes;
						tm.tm_sec = seconds;
						time_t timestamp = timegm(&tm);
						if (timestamp != (time_t)-1) {
							struct timespec gpsTime;
							gpsTime.tv_sec = timestamp;
							gpsTime.tv_nsec =0;
							_lastGPSTime.gpsTime = gpsTime;
							_lastGPSTime.timestamp = now;
							_lastGPSTime.isValid 	= 	true;
							gotTime = true;
						}
					}
					
					if(flags & 0x01) {   // gnssFixOK
						if(fixType >= 2){  // 2D-fix
							_lastLocation.latitude = lat * 1e-7;
							_lastLocation.longitude =  lon * 1e-7;
							_lastLocation.isValid = true;
						}
						
						if(fixType >= 3){ // 3D-fix
							_lastLocation.altitude = hMSL  * 1e-3;
							_lastLocation.altitudeIsValid = true;
						}
					}
					
					_lastLocation.DOP = pDOP * 0.01;
					_lastLocation.numSat = numSV;
					_lastLocation.timestamp = now;
					 
					// do we assume that velocity is valid when there is any fix?
					if((flags & 0x01) &&  (fixType >= 2)){
						_lastVelocity.heading 	= headMot * 1e-5;
						_lastVelocity.speed 		= gSpeed * 0.0019438444924406;		// convert mm/s to  knots
						_lastVelocity.isValid 	= 	true;
						_lastVelocity.timestamp = now;
					}
	 
					pthread_mutex_unlock (&_mutex);
					
					if(gotTime){
						// check against clock */
						
						time_t diffSecs = abs( _lastGPSTime.gpsTime.tv_sec - utc.tv_sec);
						pthread_mutex_unlock (&_mutex);
						
						// detect clock difference
						if(diffSecs  > 1){
							if(_timeSyncCB)
								(_timeSyncCB)(diffSecs,_lastGPSTime.gpsTime );
						}
					}
				}
				
				break;
				
			default:
				
#if DEBUG_UBX
				printf("UBX: (%02x,%02x) %zu \n", ubx_class, ubx_id, length);
				dumpHex(buffer, length, 0); printf("\n");
#endif
				break;
		}
	}
	
	
}

 
// MARK: -  NMEA decode
 
// call then when _nmea.process  is true
void GPSmgr:: processNMEA(u_int8_t *buffer, size_t length)  {
	
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now );
	
	struct timespec utc;
	clock_gettime(CLOCK_REALTIME, &utc );
 
	const char*  sentence =  (const char* )(buffer);
	
	switch (minmea_sentence_id(sentence, false)) {
			
			//Recommended Minimum
		case MINMEA_SENTENCE_RMC: {
			struct minmea_sentence_rmc frame;
			if (minmea_parse_rmc(&frame, sentence)) {
				
				if(frame.valid){
					
//					printf(" process MINMEA_SENTENCE_RMC\n");

					pthread_mutex_lock (&_mutex);
					memset((void*)&_lastVelocity, 0, sizeof(_lastVelocity));
					
					double heading =  minmea_tofloat(&frame.course);
					double speed =  minmea_tofloat(&frame.speed);
 
					if( !isnan(heading) && !isnan(speed)) {
						
						_lastVelocity.heading 	= heading;
						_lastVelocity.speed 		= speed ;
						_lastVelocity.isValid 	= 	true;
						_lastVelocity.timestamp = now;
						
//  						printf("%f mph %f deg\n",  minmea_tofloat(&frame.speed) * 1.15078 , heading);
					}
					
					bool gotTime = false;
					
					struct timespec gpsTime;				// GPS time
					if(minmea_gettime( &gpsTime, &frame.date, &frame.time) == 0){
						_lastGPSTime.gpsTime = gpsTime;
						_lastGPSTime.timestamp = now;
						_lastGPSTime.isValid 	= 	true;
						gotTime = true;
					}
					
					if(gotTime){
						// check against clock */
						
						time_t diffSecs = abs( _lastGPSTime.gpsTime.tv_sec - utc.tv_sec);
						pthread_mutex_unlock (&_mutex);
						
						// detect clock difference
						if(diffSecs  > 1){
							if(_timeSyncCB)
								(_timeSyncCB)(diffSecs,_lastGPSTime.gpsTime );
						}
					}
				}
				
			} break;
			
		case MINMEA_SENTENCE_GGA: {
			struct minmea_sentence_gga frame;
			if (minmea_parse_gga(&frame, sentence)) {
				
	//			printf(" process MINMEA_SENTENCE_GGA\n");

				if(frame.fix_quality >=  1 &&  frame.fix_quality <= 5 ) {
					pthread_mutex_lock (&_mutex);
					memset((void*)&_lastLocation, 0, sizeof(_lastLocation));
					
					
					double latitude =  minmea_tocoord(&frame.latitude);
					double longitude =  minmea_tocoord(&frame.longitude);
					
					if( !isnan(latitude) && !isnan(longitude)) {
						_lastLocation.latitude = latitude;
						_lastLocation.longitude = longitude;
						_lastLocation.isValid = true;
					}
					
					double altitude =  minmea_tofloat(&frame.altitude);
					if( !isnan(altitude) && frame.altitude_units == 'M'){
						_lastLocation.altitude = altitude  ; // tenths of meter
						_lastLocation.altitudeIsValid = true;
					}
					
					double hdop =  minmea_tofloat(&frame.hdop);
					if( !isnan(hdop)) {
						_lastLocation.DOP = int(hdop * 10);
					}
 
					_lastLocation.numSat = frame.satellites_tracked;
					_lastLocation.timestamp = now;
					pthread_mutex_unlock (&_mutex);
				}
			}
			
		} break;
			
		default:
			break;
			
		}
	}
	
}
  

// MARK: -  GPSReader thread

void GPSmgr::GPSReader(){
 
	dbuf   buff;
	
	typedef enum  {
		STATE_INIT = 0,
		STATE_SYNC,
		STATE_CLASS,
		STATE_ID,
		STATE_LEN1,
		STATE_LEN2,
		STATE_PAYLOAD,
		STATE_CHECKSUM,
		STATE_NMEA,
		STATE_ERROR
	}ubx_state_t;
	
	ubx_state_t ubx_state = STATE_INIT;
	uint8_t		ubx_class = 0;
	uint8_t		ubx_id	 = 0;
	uint16_t		ubx_length = 0;
	
	uint8_t			ubx_chk[2] = {0,0};
	UBX_checksum	checksum;
	
	uint16_t		ubx_payload_offset = 0;
 
 
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
//		struct timeval selTimeout;
//		selTimeout.tv_sec = 0;       /* timeout (secs.) */
//		selTimeout.tv_usec = 1000;            /* 100 microseconds */
		
		/* back up master */
		fd_set dup = _master_fds;
		
		int numReady = select(_max_fds+1, &dup, NULL, NULL, NULL); // &selTimeout);
		if( numReady == -1 ) {
			perror("select");
		}
		
		if ((_fd != -1)  && FD_ISSET(_fd, &dup)) {
			
	 
			u_int8_t c;
			size_t nbytes =  (size_t)::read( _fd, &c, 1 );
			
			if(nbytes == 1){
				
				switch (ubx_state) {
						
					case  STATE_INIT:
						if(c == 0xb5){
							ubx_class = 0;
							ubx_id	 = 0;
							ubx_length = 0;
							ubx_payload_offset = 0;
							checksum.reset();
							ubx_state = STATE_SYNC;
						}
						else if (c == '$'){
							ubx_state = STATE_NMEA;
							buff.reset();
							buff.append_char(c);
						}
						break;
						
					case STATE_NMEA:
					{
						if(c == '\r') break;
						if(c ==  '\n') {
							buff.append_char(0);
							processNMEA(buff.data(),buff.size());
		 
							buff.reset();
							ubx_state = STATE_INIT;
						}
						else
						{
							buff.append_char(c);
						}
					}
						break;
						
					case  STATE_SYNC:
						if(c == 0x62){
							ubx_state = STATE_CLASS;
						}
						else
							ubx_state = STATE_INIT;
						break;
						
					case  STATE_CLASS:
						ubx_class = c;
						checksum.add(c);
						ubx_state = STATE_ID;
						break;
						
					case  STATE_ID:
						ubx_id = c;
						checksum.add(c);
						ubx_state = STATE_LEN1;
						break;
						
					case  STATE_LEN1:
						ubx_length = c;
						checksum.add(c);
						ubx_state = STATE_LEN2;
						break;
						
					case  STATE_LEN2:
						ubx_length |= ((uint16_t) c) << 8;
						checksum.add(c);
						ubx_state = STATE_PAYLOAD;
						buff.reset();
						break;
						
					case STATE_PAYLOAD:
						if(buff.size() < ubx_length){
							buff.append_char(c);
							checksum.add(c);
						}
						else {
							ubx_chk[0] = c;
							ubx_state = STATE_CHECKSUM;
						}
						break;
						
					case STATE_CHECKSUM:
					{
						ubx_chk[1] = c;
						
						if( checksum.validate(ubx_chk[0], ubx_chk[1])) {
							processUBX(ubx_class, ubx_id, buff.data(),buff.size());
							ubx_state	= STATE_INIT;
						}
					}
						
						break;
						
					default:
						break;
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
 
