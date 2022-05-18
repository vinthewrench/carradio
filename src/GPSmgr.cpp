//
//  GPSmgr.cpp
//  GPStest
//
//  Created by Vincent Moscaritolo on 5/18/22.
//

#include "GPSmgr.hpp"
#include <fcntl.h>
#include <errno.h> // Error integer and strerror() function
#include "ErrorMgr.hpp"
 
#include "utm.hpp"




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

GPSmgr::GPSmgr() : _nmea( (void*)_nmeaBuffer, sizeof(_nmeaBuffer), this ){
	_isSetup = false;
	_isRunning = true;
	FD_ZERO(&_master_fds);
	_max_fds = 0;
	
 	_fd = -1;
//	_nmea.setBuffer( (void*)_nmeaBuffer, sizeof(_nmeaBuffer));
 	_nmea.clear();
	
	
}

GPSmgr::~GPSmgr(){
	stop();
	
	FD_ZERO(&_master_fds);
	_max_fds = 0;

}



bool GPSmgr::begin(const char* path, speed_t speed){
	int error = 0;
	
	return begin(path, speed, error);
}


bool GPSmgr::begin(const char* path, speed_t speed,  int &error){

	_isSetup = false;
	struct termios options;
	
	int fd ;
	
	if((fd = ::open( path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_NDELAY  )) <0) {
		ELOG_ERROR(ErrorMgr::FAC_DEVICE, 0, errno, "OPEN %s", path);
		error = errno;
		return false;
	}
	
	fcntl(fd, F_SETFL, 0);      // Clear the file status flags
	
	// Back up current TTY settings
	if( tcgetattr(fd, &_tty_opts_backup)<0) {
		ELOG_ERROR(ErrorMgr::FAC_DEVICE, 0, errno, "tcgetattr %s", path);
		error = errno;
		return false;
	}
	
	cfmakeraw(&options);
	options.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
	options.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
	options.c_cflag &= ~CSIZE; // Clear all bits that set the data size
	options.c_cflag |= CS8; // 8 bits per byte (most common)
	// options.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control 	options.c_cflag |=  CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
	options.c_cflag |=  CRTSCTS; // DCTS flow control of output
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
	
	cfsetospeed (&options, speed);
	cfsetispeed (&options, speed);
	
	if (tcsetattr(fd, TCSANOW, &options) < 0){
		ELOG_ERROR(ErrorMgr::FAC_DEVICE, 0, errno, "Unable to tcsetattr %s", path);
		error = errno;
		return false;
	}
	
	_fd = fd;
	
	// add to read set
	safe_fd_set(_fd, &_master_fds, &_max_fds);
 
	_nmea.clear();
 
	_isSetup = true;
	_isRunning = true;

	pthread_create(&_TID, NULL,
										  (THREADFUNCPTR) &GPSmgr::GPSReaderThread, (void*)this);

	return _isSetup;
}

void GPSmgr::stop(){
	
	if(_isSetup && _fd > -1){
		
		pthread_mutex_lock (&_mutex);
		_isRunning = false;
		pthread_cond_signal(&_cond);
		pthread_mutex_unlock (&_mutex);
 
		pthread_join(_TID, NULL);

 		// Restore previous TTY settings
		tcsetattr(_fd, TCSANOW, &_tty_opts_backup);
		close(_fd);
		safe_fd_clr(_fd, &_master_fds, &_max_fds);
		_fd = -1;
 	}
	
	_isSetup = false;
}



bool GPSmgr::reset(){

	return true;
}
 

// MARK: -  GPSReader thread
 



// call then when _nmea.process  is true
void GPSmgr::processNMEA(){
	
	
	string msgID =  string(_nmea.getMessageID());
	
	/*
	 
	 ublox NEO-M9N
	 receive signals from the GPS, GLONASS, Galileo, and BeiDou constellations with ~1.5 meter accuracy.
	 
	 $GP	GPS			US
	 $GL GLONASS		RU
	 $GA Galileo  		EU
	 $BD  BeiDou 	(China)
	 
	 $GN mix..
	 
	 
	 message
	 GGA	Global Positioning System Fix Data
	 
	 */
	
	
	if( msgID ==  "GGA") {
		//Global Positioning System Fix Data
		
		if(_nmea.isValid()){
			
			long  altitude;
			long  Zone;
			char  Hemisphere;
			double Easting;
			double Northing;
			
#ifndef PI
#define PI           3.14159265358979323e0    /* PI                        */
#endif
			
			double latRad = (PI/180.) * (_nmea.getLatitude() / 1e6);
			double lonRad = (PI/180.) * (_nmea.getLongitude() / 1e6);
			
			
			if( Convert_Geodetic_To_UTM(latRad, lonRad,
												 &Zone, &Hemisphere, &Easting, &Northing ) == UTM_NO_ERROR){
				
				printf("GGA [%c] ", _nmea.getNavSystem());
				printf("  UTM %d%c %ld %ld ", (int)Zone, Hemisphere, (long) Easting, (long) Northing);
				
#define MM2FT 	0.0032808399
				if(_nmea.getAltitude(altitude) ) {
					printf("%.1f ft", (float)(altitude * MM2FT));
				}
				
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
				
				
				printf(" s %.1f, Sats: %d",
						 _nmea.getHDOP() / 10.,
						 _nmea.getNumSatellites());
				
				printf("\n");
				
			}
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
	printf("start GPSReader\n");
	
	_nmea.setUnknownSentenceHandler(UnknownSentenceHandler);
	
	while(_isRunning){
		
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
			
			for(bool readMore = true; readMore; ){
				
				u_int8_t c;
				size_t nbytes =  (size_t)::read( _fd, &c, 1 );
				
				if(nbytes == 1){
					if(_nmea.process(c)){
						processNMEA();
					}
				}
				else if( nbytes == -1) {
					readMore = false;
					int lastError = errno;
					if(lastError != EAGAIN) {
						perror("read");
					}
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
