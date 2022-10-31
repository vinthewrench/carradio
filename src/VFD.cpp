//
//  VFD.cpp
//  vfdtest
//
//  Created by Vincent Moscaritolo on 4/13/22.
//

#include "VFD.hpp"
#include <fcntl.h>
#include <errno.h> // Error integer and strerror() function
#include <math.h>
#include "ErrorMgr.hpp"
#include "Utils.hpp"

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

void VFD::drawScrollBar(uint8_t topbox,  float bar_height, float starting_offset){
 
	uint8_t  rightbox = this->width() -1;
	uint8_t  leftbox = rightbox - 2;
	uint8_t  bottombox = 63;
	uint8_t  scroll_height = bottombox - topbox -2;
	uint8_t  bar_size =  ceil(scroll_height * bar_height);
	uint8_t  offset =  ((scroll_height  - bar_size) * starting_offset) + topbox +1;
	
//	if((bar_size + offset) >= bottombox)
//		bar_size = bottombox - offset;
//
//	printf("drawScrollBar(%d,%.2f,%.2f)  offset = %d bar_size:%d scroll_height = %d\n",
//			 topbox,bar_height, starting_offset, offset, bar_size, scroll_height);
	
	uint8_t buff2[] = {
		VFD_OUTLINE,leftbox, topbox,rightbox, bottombox,
		
		VFD_CLEAR_AREA,
		static_cast<uint8_t>(leftbox+1), static_cast<uint8_t> (topbox+1),
		static_cast<uint8_t>(rightbox-1),static_cast<uint8_t>(bottombox-1),
		
		VFD_SET_AREA,
		static_cast<uint8_t>(leftbox+1),static_cast<uint8_t>(offset),
		static_cast<uint8_t>(rightbox-1),static_cast<uint8_t>(offset + bar_size)

	};
	writePacket(buff2, sizeof(buff2), 0);
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


#warning FIX Noritake VFD 60H bug

// there is some kind of bug in the Noritake VFD where id you send
// VFD_CLEAR_AREA  followed by a 0x60, it screws up the display
// To send commands as hexadecimal, prefix the 2 bytes using character 60H.
// To send character 60H to the display, send 60H twice.

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
// MARK: -  Print scrollable lines

// for 5x5 font
static uint8_t string_pixel_Width(string str, VFD::font_t font = VFD::FONT_MINI){
	uint8_t nonSpace = 0;
	uint8_t spaces = 0;

	for(auto c:str){
		if(std::isspace(c)) spaces++; else nonSpace++;
	}
	uint length = 0;
	switch (font) {
		case VFD::FONT_MINI:
			length = (nonSpace*4) + (spaces*2) + 3;
			break;

		case VFD::FONT_5x7:
			length = (nonSpace*6) + (spaces*6);
			break;

		case VFD::FONT_10x14:
			length = (nonSpace*11) + (spaces*11);
			break;

		default:
			break;
	}

	 
	return length;}

bool VFD:: printLines(uint8_t y, uint8_t step,
							 stringvector lines,
							 uint8_t firstLine,
							 uint8_t maxLines,
	 						 uint8_t maxchars,
							 VFD::font_t font,
							 uint8_t max_pixels) {
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
		
		// this text needs to be scrolled
		
		// quick scan for max line length skip spaces
		uint8_t longest_pixel_width  = 0;
		
		for(auto line:lines){
			auto length = string_pixel_Width(line,font);
			if(length> longest_pixel_width )longest_pixel_width = length;
 		}
	 
		auto maxFirstLine = lineCount - maxLines;
		if(firstLine > maxFirstLine) firstLine = maxFirstLine;
	 
		auto count =  lineCount - firstLine;
		if( count > maxLines) count = maxLines;
		
		for(auto i = firstLine; i < firstLine + count; i ++){
			setCursor(0, y);
			
			string str = lines[i];
			str = truncate(str,  maxchars);

			auto pixel_width = string_pixel_Width(str,font);
			if(pixel_width < longest_pixel_width && max_pixels > 0){
				
				// what I really need is a way to clear to a givven point
				// from the cursor position.  but Noritake doesnt have that,
		 
				uint8_t  rightbox = max_pixels;
				uint8_t  leftbox = rightbox - (longest_pixel_width -pixel_width);
				uint8_t  topbox = y - step;
				uint8_t  bottombox = y;
	 
				uint8_t buff2[] = {
					VFD_CLEAR_AREA,
					static_cast<uint8_t>(leftbox+1), static_cast<uint8_t> (topbox+1),
					static_cast<uint8_t>(rightbox-1),static_cast<uint8_t>(bottombox-1),
				};
				writePacket(buff2, sizeof(buff2), 0);
		 
			}
 
			success = printPacket("%-*s",maxchars, str.c_str());
			if(!success) break;
			y += step;
		}
	}
 
	return success;
}
// MARK: -  multi column version
 
bool VFD:: printRows(uint8_t y, uint8_t step,
								vector<vector <string>>  columns,
 								uint8_t firstLine,
								uint8_t maxLines,
								uint8_t maxchars,
								VFD::font_t font,
								uint8_t max_pixels) {
	  bool success = false;
	  
	  auto lineCount = columns.size();
	  
	// quick scan for max line length skip spaces
	uint8_t longest_pixel_width  = 0;
	uint8_t longest_col2_pixel_width  = 0;
		uint8_t col2_start  = 0;

	for(auto row:columns){

		auto length = string_pixel_Width(row[0],font);
		if(length> longest_pixel_width )longest_pixel_width = length;
		
		if(row.size() > 1 &&  !row[1].empty()){
			 length = string_pixel_Width(row[1],font);
			if(length > longest_col2_pixel_width )longest_col2_pixel_width = length;
		}
	}
	
		col2_start = width() - longest_col2_pixel_width - 4;

	  if(maxLines >= lineCount){
		  //ignore the offset and draw all.
		  for(int i = 0; i < lineCount; i ++){
			  setCursor(0, y);
//			  success = printPacket("%-40s", lines[i].c_str());
			  if(!success) break;
			  y += step;
		  }
	  }
	  else {
		  
		  // this text needs to be scrolled
			  
		  auto maxFirstLine = lineCount - maxLines;
		  if(firstLine > maxFirstLine) firstLine = maxFirstLine;
		
		  auto count =  lineCount - firstLine;
		  if( count > maxLines) count = maxLines;
		  
		  for(auto i = firstLine; i < firstLine + count; i ++){
			  setCursor(0, y);
	 
			  vector<string> row = columns[i];
			  
			  string str = row[0];
			  string col2 = "";
			  if(row.size() > 1) col2 = row[1];
			  
	 		  str = truncate(str,  maxchars);

			  auto pixel_width = string_pixel_Width(str,font);
		 
			  if(pixel_width < longest_pixel_width && max_pixels > 0){
				  
				  // what I really need is a way to clear to a given point
				  // from the cursor position.  but Noritake doesnt have that,
			
				  uint8_t  rightbox = col2_start;
				  uint8_t  leftbox = rightbox - (longest_pixel_width - pixel_width);
				  uint8_t  topbox = y - step;
				  uint8_t  bottombox = y;
		
				  uint8_t buff2[] = {
					  VFD_CLEAR_AREA,
					  static_cast<uint8_t>(leftbox+1), static_cast<uint8_t> (topbox+1),
					  static_cast<uint8_t>(rightbox-1),static_cast<uint8_t>(bottombox-1),
				  };
				  writePacket(buff2, sizeof(buff2), 0);
			
			  }
	
			  setCursor(0, y);
 			  success = printPacket("%-*s",maxchars, str.c_str());
	
			  if(success && !col2.empty()){
				  setCursor(col2_start, y);
				  success = printPacket("%s", col2.c_str());
 			  }
	 
			  {
				  auto pixel_width2 = string_pixel_Width(col2,font);
				  
				  uint8_t  rightbox = width() - 5;
				  uint8_t  leftbox = rightbox - (longest_col2_pixel_width -pixel_width2);
				  uint8_t  topbox = y - step;
				  uint8_t  bottombox = y;
		
				  uint8_t buff2[] = {
					  VFD_CLEAR_AREA,
					  static_cast<uint8_t>(leftbox+1), static_cast<uint8_t> (topbox+1),
					  static_cast<uint8_t>(rightbox-1),static_cast<uint8_t>(bottombox-1),
				  };
				  writePacket(buff2, sizeof(buff2), 0);
 			  }
		
	
			  if(!success) break;
			  y += step;
		  }
	  }
	
	  return success;
}
