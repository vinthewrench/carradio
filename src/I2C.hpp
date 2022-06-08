//
//  I2C.hpp
//  coopMgr
//
//  Created by Vincent Moscaritolo on 9/10/21.
//

#pragma once

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>     /* va_list, va_start, va_arg, va_end */
#include <time.h>
#include <termios.h>
#include <stdexcept>
#include <string>

class I2C  {
 
public:
	typedef uint8_t  i2c_block_t [32];
	
	I2C();
	~I2C();
	
	bool begin(uint8_t	devAddr);
 	bool begin(uint8_t	devAddr,  int &error);
	bool begin(uint8_t	devAddr,  const char *path, int &error);

	void stop();

	bool 		isAvailable();
	uint8_t	getDevAddr() {return _devAddr;};
	
	bool writeByte(uint8_t byte);		// simple 1 byte write

	bool writeByte(uint8_t regAddr, uint8_t byte);
	bool writeWord(uint8_t regAddr, uint16_t word, bool swap = false);

	bool readByte(uint8_t& byte);	// simple 1 byte read
	bool readByte(uint8_t regAddr,  uint8_t& byte);
	
	bool readWord(uint8_t regAddr,  uint16_t& word, bool swap = false);
	bool readWord(uint8_t regAddr,  int16_t& word, bool swap = false);

	
	bool readBlock(uint8_t regAddr, uint8_t size, i2c_block_t & block );
	bool writeBlock(uint8_t regAddr, uint8_t size, i2c_block_t block );

	// stupid c++ alternative version
	bool readByte(uint8_t regAddr,  unsigned char * byte);
 	bool readBlock(uint8_t regAddr, uint8_t size, unsigned char * block );

private:

	int 			_fd;
	int 			_devAddr;
	bool 			_isSetup;
 
};

 
