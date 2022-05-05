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


//#define GU128x64D  1
#define GU126x64F  1

using namespace std;

class VFD {
public:

	typedef enum  {
		FONT_MINI = 0,
		FONT_5x7 ,
		FONT_10x14,
 	}font_t;

	VFD();
  ~VFD();
	
  bool begin(string path, speed_t speed =  B19200);		// alwsys uses a fixed address
  bool begin(string path, speed_t speed, int &error);
  void stop();

 	bool reset();
 
	bool write(string str);
	bool write(const char* str);
	bool writePacket(const uint8_t *data , size_t len , useconds_t waitusec = 50);

	bool setBrightness(uint8_t);  //  0 == off - 7 == max

	bool clearScreen();

	bool setCursor(uint8_t x, uint8_t y);
	bool setFont(font_t font);

	inline int width() {
#if GU126x64F
	 		return 126;
#elif GU128x64D
 		return 128;
#endif
 	};
	
	inline int height() {
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
