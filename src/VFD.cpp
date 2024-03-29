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
 
	uint8_t  rightbox = width() -1;
	uint8_t  leftbox = rightbox - scroll_bar_width + 1;
	uint8_t  bottombox = 63;
	uint8_t  scroll_height = bottombox - topbox -2;
	uint8_t  bar_size =  ceil(scroll_height * bar_height);
	uint8_t  offset =  ((scroll_height  - bar_size) * starting_offset) + topbox +1;
	
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
 
bool VFD:: writePacket(const uint8_t * data, size_t len, useconds_t waitusec){
	
 	if(len == 0) return true;

	bool success = false;

	// I consider this a bug in the Noritake VFD firmware.  if you send a
	// VFD_CLEAR_AREA  followed by a 0x60, it screws up the display
	// To send commands as hexadecimal, prefix the 2 bytes using character 60H.
	// To send character 60H to the display, send 60H twice.
	
	uint8_t * newBuff = NULL;
	int count_60H = 0;
	// check if we need to correct and count how many additional bytes
 	for(int i = 0; i < len ;i++)
		if(data[i] == 0x60) count_60H++;

	if(count_60H ){
		newBuff = (uint8_t *) malloc(len + count_60H);
		
		uint8_t *p = newBuff;
		
		for(int i = 0; i < len ;i++) {
			uint8_t  ch = data[i];
			*p++ = ch;
			if(ch == 0x60) *p++ = 0x60;
 		}
		len = p - newBuff;
		data = newBuff;
  	}
 
	
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
	
	
	if(newBuff) free(newBuff);
	
	return success;
}
// MARK: -  Print scrollable lines


// for 5x5 font
static uint8_t string_pixel_Width(string str, VFD::font_t font = VFD::FONT_MINI){
  	uint length = 0;
	switch (font) {
		case VFD::FONT_MINI:
		{
			bool mode_5x7 = false;

			for(auto c:str){
	 			if(c == '\x1d')
					mode_5x7 = true;
				else if(c == '\x1c')
					mode_5x7 = false;
				else if(mode_5x7)
					length +=6;
				else if(strchr("MNWW", c))
					length +=6;
				else if(strchr("@GQ", c))
					length +=5;
				else if(strchr(" !", c))
					length +=3;
				else
					length +=4;
			}
		}
	 		break;

		case VFD::FONT_5x7:
			length =  (uint)str.size() * 6;
 			break;

		case VFD::FONT_10x14:
			length =  (uint)str.size() * 11;
			break;

		default:
			break;
	}
 	return length;
 }

// for 5x5 font
static uint charcount_for_pixel_Width(string str, uint8_t max_width, VFD::font_t font = VFD::FONT_MINI){
	
	uint count = 0;
	uint length = 0;
	bool mode_5x7 = false;

	for(auto c:str){
		switch (font) {
				
			case VFD::FONT_MINI:
			{
				
				if(c == '\x1d')
					mode_5x7 = true;
				else if(c == '\x1c')
					mode_5x7 = false;
				else if(mode_5x7)
					length +=6;
				else if(strchr("MNWW#", c))
					length +=6;
				else if(strchr("@GQ", c))
					length +=5;
				else if(strchr(" !", c))
					length +=3;
				else
					length +=4;
			}
				
				break;
				
			case VFD::FONT_5x7:
				length =  (uint)str.size() * 6;
				break;
				
			case VFD::FONT_10x14:
				length =  (uint)str.size() * 11;
				break;
				
			default:
				break;
		}
 
		if(length < max_width){
			count++;
		}
		else
			break;
 	}
 
	return count;
 }



bool VFD:: printLines(uint8_t y, uint8_t step,
							 stringvector lines,
							 uint8_t firstLine,
							 uint8_t maxLines, 
							 VFD::font_t font ) {
	bool success = false;
	
	auto lineCount = lines.size();
	
	setFont(font) ;

//	printf("\nfirst: %d,  lines: %d maxLines: %d\n",(int) firstLine, (int) lineCount, (int)maxLines);
//	for(auto line:lines){
//		printf("%s\n", line.c_str());
//	}
 
	if(maxLines >= lineCount){
		//ignore the offset and draw all.
		for(int i = 0; i < lineCount; i ++){
			
			string str = lines[i].c_str();
			if(!str.empty()){
				setCursor(0, y);
				success = printPacket("%s",lines[i].c_str());
				if(!success) break;
			}
			y += step;
		}
	}
	else {
		
		// this text needs to be scrolled
		
		// quick scan for max line length skip spaces
	 
		//
		//		for(auto line:lines){
		//			auto length = string_pixel_Width(line,font);
		//			if(length> longest_pixel_width )longest_pixel_width = length;
		// 		}
		
		auto maxFirstLine = lineCount - maxLines;
		if(firstLine > maxFirstLine) firstLine = maxFirstLine;
		
		auto count =  lineCount - firstLine;
		if( count > maxLines) count = maxLines;
		
		for(auto i = firstLine; i < firstLine + count; i ++){
			
			string str = lines[i];
				
			// what I really need is a way to clear to a givven point
			// from the cursor position.  but Noritake doesnt have that,
			
			uint8_t  rightbox = width() -1 - scroll_bar_width;
			uint max_chars = charcount_for_pixel_Width(str, rightbox , font);
			str = truncate(str,  max_chars);
 			auto pixel_width = string_pixel_Width(str,font);

			uint8_t  leftbox = pixel_width - 4;
			uint8_t  topbox = y - step;
			uint8_t  bottombox = y;
			
			uint8_t buff2[] = {
				VFD_CLEAR_AREA,
				static_cast<uint8_t>(leftbox), static_cast<uint8_t> (topbox+1),
				static_cast<uint8_t>(rightbox),static_cast<uint8_t>(bottombox-1),
			};
			writePacket(buff2, sizeof(buff2), 0);
			
			setCursor(0, y);
			success = printPacket("%s", str.c_str());
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
								uint8_t col1_start,
							VFD::font_t font ) {
	bool success = false;
	
	auto lineCount = columns.size();
	
	// quick scan for max line length skip spaces
	//	uint8_t longest_col2_pixel_width  = 0;
	uint8_t longest_col1_pixel_width  = 0;
	uint8_t col2_start  = 0;
	
	setFont(font) ;
	
	for(auto &row:columns){
		uint length = 0;
		
		if(row.size() > 0 &&  !row[0].empty()){
			length = string_pixel_Width(row[0],font);
			if(length > longest_col1_pixel_width )longest_col1_pixel_width = length;
		}
	}
	
	col2_start =  col1_start + longest_col1_pixel_width + 2;
	
	if(maxLines >= lineCount){
		//ignore the offset and draw all.
		for(int i = 0; i < lineCount; i ++){
			
			vector<string> row = columns[i];
			string str = row[0];
			string col2 = "";
			if(row.size() > 1) col2 = row[1];
			
			setCursor(col1_start, y);
			success = printPacket("%s",str.c_str());
			if(!success) break;
			
			setCursor(col2_start, y);
			success = printPacket("%s", col2.c_str());
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
			
			vector<string> row = columns[i];
			string str = row[0];
			string col2 = "";
			if(row.size() > 1) col2 = row[1];
			
			// erase to end of column
			// what I really need is a way to clear to a given point
			// from the cursor position.  but Noritake doesnt have that,
			
			uint max_chars = charcount_for_pixel_Width(str, col2_start , font);
			str = truncate(str,  max_chars);
			auto pixel_width = string_pixel_Width(str,font);
			
			uint8_t  rightbox = col2_start -1;
			uint8_t  leftbox =  col1_start + pixel_width;
			uint8_t  topbox = y - step;
			uint8_t  bottombox = y;
			
			// erase to end of column 1
 			uint8_t buff1[] = {
				VFD_CLEAR_AREA,
				static_cast<uint8_t>(leftbox), static_cast<uint8_t> (topbox+1),
				static_cast<uint8_t>(rightbox),static_cast<uint8_t>(bottombox-1),
			};
			writePacket(buff1, sizeof(buff1), 0);
			// write string
			setCursor(col1_start, y);
			success = printPacket("%s",str.c_str());
			
			// erase to end of column 2
			auto pixel_width2 = string_pixel_Width(col2,font);
			rightbox = width() - scroll_bar_width -1;
			leftbox = col2_start + pixel_width2 - 2;
			
			uint8_t buff2[] = {
				VFD_CLEAR_AREA,
				static_cast<uint8_t>(leftbox), static_cast<uint8_t> (topbox+1),
				static_cast<uint8_t>(rightbox),static_cast<uint8_t>(bottombox-1),
			};
			writePacket(buff2, sizeof(buff2), 0);
			
			if(success && !col2.empty()){
				setCursor(col2_start, y);
				success = printPacket("%s", col2.c_str());
			}
			
			if(!success) break;
			y += step;
		}
	}
	
	return success;
}
