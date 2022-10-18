//
//  VFD.hpp
//  vfdtest
//
//  Created by Vincent Moscaritolo on 4/13/22.
//
#pragma once
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <termios.h>
#include <string>
#include "CommonDefs.hpp"


//#define GU128x64D  1
#define GU128x64D  1

using namespace std;

class VFD {
	
	friend class DisplayMgr;
	
public:

	static constexpr uint8_t VFD_OUTLINE = 0x14;
	static constexpr uint8_t VFD_CLEAR_AREA = 0x12;
	static constexpr uint8_t VFD_SET_AREA = 0x11;
	static constexpr uint8_t VFD_SET_CURSOR = 0x10;
	static constexpr uint8_t VFD_SET_WRITEMODE = 0x1A;

	typedef enum  {
		FONT_MINI = 0,
		FONT_5x7 ,
		FONT_10x14,
 	}font_t;

	VFD();
  ~VFD();
	
  bool begin(const char* path, speed_t speed =  B19200);		// alwsys uses a fixed address
  bool begin(const char* path, speed_t speed, int &error);
  void stop();

 	bool reset();
 
	bool write(string str);
	bool write(const char* str);
	bool writePacket(const uint8_t *data , size_t len , useconds_t waitusec = 50);

	bool printPacket(const char *fmt, ...);
	
	bool printLines(uint8_t y, uint8_t step, stringvector lines,  uint8_t firstLine,  uint8_t maxLines);
		
	bool setBrightness(uint8_t);  //  0 == off - 7 == max
	bool setPowerOn(bool setOn);
	
	bool clearScreen();
	
	void drawScrollBar(uint8_t top, uint8_t bar_height, uint8_t offset);
 
	bool setCursor(uint8_t x, uint8_t y);
	bool setFont(font_t font);

	inline uint16_t width() {
#if GU126x64F
	 		return 126;
#elif GU128x64D
 		return 128;
#endif
 	};
	
	inline uint16_t height() {
#if GU126x64F
			return 64;
#elif GU128x64D
		return 64;
#endif
	};

private:
 
	int	 	_fd;
	bool		_isSetup;

	struct termios _tty_opts_backup;

};
