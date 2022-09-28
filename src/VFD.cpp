//
//  VFD.cpp
//  vfdtest
//
//  Created by Vincent Moscaritolo on 4/13/22.
//

#include "VFD.hpp"
#include <fcntl.h>
#include <errno.h> // Error integer and strerror() function
#include "ErrorMgr.hpp"


VFD::VFD(){
	_isSetup = false;
	_fd = -1;
	
	
}

VFD::~VFD(){
	stop();
}


bool VFD::begin(const char* path, speed_t speed){
	int error = 0;
	
	return begin(path, speed, error);
}


bool VFD::begin(const char* path, speed_t speed,  int &error){
	
#if defined(__APPLE__)
	_fd  = 1;
#else
	_isSetup = false;
	struct termios options;
	
	int fd ;
	
	if((fd = ::open( path, O_RDWR | O_NOCTTY)) <0) {
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
#endif
	
	_isSetup = true;
	return _isSetup;
}

void VFD::stop(){
	
	if(_isSetup && _fd > -1){
	
#if defined(__APPLE__)
#else
		// Restore previous TTY settings
		tcsetattr(_fd, TCSANOW, &_tty_opts_backup);
		close(_fd);
		_fd = -1;
#endif
	}
	
	_isSetup = false;
}



bool VFD::reset(){
	
	uint8_t buffer[] = {0x19};
	return  writePacket(buffer, sizeof(buffer), 1000);
}

bool VFD::setBrightness(uint8_t level){
	
	level = level > 7?7:level;
	level |= 0xF8;
	uint8_t buffer[] = {0x1b, level};
	
	return  writePacket(buffer, sizeof(buffer), 50);
}


bool  VFD::setPowerOn(bool setOn){
	
	uint8_t buffer[] = {0x1b, 0x00};
 	buffer[1] = setOn?0x50:0x46 ;
	return  writePacket(buffer, sizeof(buffer), 50);
}



bool VFD::setCursor(uint8_t x, uint8_t y){
	uint8_t buffer[] = {0x10, x,y};
	
	return  writePacket(buffer, sizeof(buffer), 50);
}

bool  VFD::setFont(font_t font){
	uint8_t buffer[] = {0x00};
	
	switch(font) {
		case FONT_MINI: 	buffer[0] = 0x1c; break;
		case FONT_5x7:	 	buffer[0] = 0x1d; break;
		case FONT_10x14:	buffer[0] = 0x1e; break;
		default:
			return false;
	}
	
	return  writePacket(buffer, sizeof(buffer), 500);
	
}


bool VFD::clearScreen(){
	
	uint8_t buffer[] = {0x12, 0, 0, 0xff, 0xff};
	
	return  writePacket(buffer, sizeof(buffer), 50);
}



bool VFD::write(const char* str){
	return  writePacket( (uint8_t *) str, strlen(str), 500);
	
	
}

bool VFD:: write(string str){
	
	return  writePacket( (uint8_t *) str.c_str(), str.size(), 500);
}

bool VFD::printPacket(const char *fmt, ...){
	bool success = false;

	char *s;
	va_list args;
	va_start(args, fmt);
	vasprintf(&s, fmt, args);
	
	success = write(s);
	free(s);
	va_end(args);
	
	return success;
}


#define PACKET_MODE 1

bool VFD:: writePacket(const uint8_t * data, size_t len, useconds_t waitusec){
	
	bool success = false;
	
#if PACKET_MODE
	constexpr size_t blocksize = 32;
	uint8_t buffer[blocksize + 4 ];
	
	size_t bytesLeft = len;
	while(bytesLeft > 0) {
		
		uint8_t len = bytesLeft < 28?bytesLeft:28;
		uint8_t checksum = 0;
		
		uint8_t *p = buffer;
		*p++ = 0x02;
		*p++ =  len;
		
		for(int i = 0; i < len; i++){
			checksum += *data;
			*p++ = *data++;
		}
		*p++ = checksum;
		*p++ =  0x03;
		
#if 1
		for(int i = 0; i < len +4; i++){
			success = (::write(_fd,&buffer[i] , 1) == 1);
			if(!success) return false;
			usleep(10);
		}
		
#else
		success = (::write(_fd,buffer , len) == len);
		if(!success) break;
		usleep(waitusec);
#endif
		
		usleep(waitusec);
		
		if(!success) break;
		bytesLeft-=len;
	}
	
#else
	
	success = (::write(_fd, data , len) == len);
	
	if(!success)
		printf("error %d\n",errno);
	
	// for(int i = 0; i<len; i++){
	// 	success = (::write(_fd, &data[i], 1) == 1);
	//  	if(!success) break;
	// }
	 usleep(waitusec);
#endif
	
	return success;
}


bool VFD:: printLines(uint8_t y, uint8_t step,
							 stringvector lines,
							 uint8_t firstLine,
							 uint8_t maxLines){
	bool success = false;
	
	auto lineCount = lines.size();

	if(maxLines >= lineCount){
		//ignore the offset and draw all.
		for(int i = 0; i < lineCount; i ++){
			setCursor(0, y);			
			success = printPacket("%-40s", lines[i].c_str());
			if(!success) break;
			y += step;
		}
	}
	else {
		auto maxFirstLine = lineCount - maxLines;
		if(firstLine > maxFirstLine) firstLine = maxFirstLine;
	 
		auto count =  lineCount - firstLine;
		if( count > maxLines) count = maxLines;
		
		for(auto i = firstLine; i < firstLine + count; i ++){
			setCursor(0, y);
			success = printPacket("%-40s", lines[i].c_str());
			if(!success) break;
			y += step;
		}
	}
 
	return success;
}




//
//			//		size_t max_lines = 5;
//			size_t totalLines = lines.size();
//			size_t start_line = _lineOffset;
//			if(start_line > totalLines - 1)  start_line = totalLines -1;
//
//			printf("start_line = %zu\n", start_line);
//			for(size_t i = start_line; i < totalLines; i++){
//				printf("%2zu |%s|\n", i, lines[i].c_str());
//			}
//
//
//			for(size_t i = start_line; i < totalLines; i++){
//				string str = lines[i];
//				_vfd.setCursor(10, row);
//				_vfd.write(str);
//				row+=7;
//			}
