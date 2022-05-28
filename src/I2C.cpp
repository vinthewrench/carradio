//
//  I2C.cpp
//  coopMgr
//
//  Created by Vincent Moscaritolo on 9/10/21.
//

#include "I2C.hpp"
#include <errno.h>
#include <sys/ioctl.h>                                                  // Serial Port IO Controls
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include "ErrorMgr.hpp"


// I2C definitions

#define I2C_SLAVE	0x0703
#define I2C_SMBUS	0x0720	/* SMBus-level access */

#define I2C_SMBUS_READ	1
#define I2C_SMBUS_WRITE	0

// SMBus transaction types

#define I2C_SMBUS_QUICK		    0
#define I2C_SMBUS_BYTE		    1
#define I2C_SMBUS_BYTE_DATA	    2
#define I2C_SMBUS_WORD_DATA	    3
#define I2C_SMBUS_PROC_CALL	    4
#define I2C_SMBUS_BLOCK_DATA	    5
#define I2C_SMBUS_I2C_BLOCK_BROKEN  6
#define I2C_SMBUS_BLOCK_PROC_CALL   7		/* SMBus 2.0 */
#define I2C_SMBUS_I2C_BLOCK_DATA    8

// SMBus messages

#define I2C_SMBUS_BLOCK_MAX	32	/* As specified in SMBus standard */
#define I2C_SMBUS_I2C_BLOCK_MAX	32	/* Not specified but we use same structure */

// Structures used in the ioctl() calls

union i2c_smbus_data
{
  uint8_t  byte ;
  uint16_t word ;
  uint8_t  block [I2C_SMBUS_BLOCK_MAX + 2] ;	// block [0] is used for length + one more for PEC
} ;

struct i2c_smbus_ioctl_data
{
  char read_write ;
  uint8_t command ;
  int size ;
  union i2c_smbus_data *data ;
} ;

static inline int i2c_smbus_access (int fd, char rw, uint8_t command, int size, union i2c_smbus_data *data)
{
  struct i2c_smbus_ioctl_data args ;

  args.read_write = rw ;
  args.command    = command ;
  args.size       = size ;
  args.data       = data ;
  return ::ioctl (fd, I2C_SMBUS, &args) ;
}

#ifndef I2C_BUS_DEV_FILE_PATH
#define I2C_BUS_DEV_FILE_PATH "/dev/i2c-1"
#endif /* I2C_SLAVE */

 
I2C::I2C(){
	_isSetup = false;
	_fd = -1;
	_devAddr = 00;
}


I2C::~I2C(){
	stop();
	
}
 
bool I2C::begin(uint8_t	devAddr){
	int error = 0;

	return begin(devAddr, error);
}


bool I2C::begin(uint8_t	devAddr,   int &error){
	static const char *ic2_device = "/dev/i2c-1";
 
	_isSetup = false;
	int fd ;

	if((fd = open( ic2_device, O_RDWR)) <0) {
 
		ELOG_ERROR(ErrorMgr::FAC_I2C, 0, errno, "OPEN %s", ic2_device);

		error = errno;
		return false;
	}
	
	if (::ioctl(fd, I2C_SLAVE, devAddr) < 0) {
		
		ELOG_ERROR(ErrorMgr::FAC_I2C, devAddr, errno, "I2C_SLAVE");
		error = errno;
		return false;
	}

	_fd = fd;
	_isSetup = true;
	_devAddr = devAddr;
	
	return _isSetup;
}


void I2C::stop(){
	
	if(_isSetup){
		close(_fd);
		_devAddr = 00;
	}
	
	_isSetup = false;
}

bool I2C::isAvailable(){

	return _isSetup;
}

bool I2C::writeByte( uint8_t b1){

	if(!_isSetup) return false;

	if (i2c_smbus_access(_fd,I2C_SMBUS_WRITE,b1,
									I2C_SMBUS_BYTE, NULL) < 0){

			ELOG_ERROR(ErrorMgr::FAC_I2C, _devAddr, errno,  "I2C_SMBUS_WRITE BYTE () ");
		return false;
	}
	
	return   true;
}


bool I2C::writeByte(uint8_t regAddr, uint8_t b1){
	
	if(!_isSetup) return false;

	union i2c_smbus_data data = {.byte = b1};
  
	if(i2c_smbus_access (_fd, I2C_SMBUS_WRITE, regAddr, I2C_SMBUS_BYTE_DATA, &data) < 0){
		
		ELOG_ERROR(ErrorMgr::FAC_I2C, _devAddr, errno,  "I2C_SMBUS_WRITE BYTE (%02x) ", regAddr);
		return false;
	}
	
	return   true;
}


bool I2C::writeWord(uint8_t regAddr, uint16_t word, bool swap ){

	if(!_isSetup) return false;

	union i2c_smbus_data data;
	
	if(swap){
		data.word =((word << 8) & 0xff00) | ((word >> 8) & 0x00ff);
	}
	else {
		data.word = word;
	}

	if(i2c_smbus_access (_fd, I2C_SMBUS_WRITE, regAddr, I2C_SMBUS_WORD_DATA, &data) < 0){
		
		ELOG_ERROR(ErrorMgr::FAC_I2C, _devAddr, errno,  "I2C_SMBUS_WRITE WORD (%02x) ", regAddr);
 	
		return false;
	}
	
	return   true;
}

bool I2C::readByte(uint8_t& byte){
	
	if(!_isSetup) return false;

	union i2c_smbus_data data;
	
	if(i2c_smbus_access (_fd, I2C_SMBUS_READ, 0, I2C_SMBUS_BYTE, &data) < 0){
		
		ELOG_ERROR(ErrorMgr::FAC_I2C, _devAddr, errno,  "I2C_SMBUS_WRITE BYTE () ");
 
		return false;
	}

	byte = data.byte & 0xFF;
	return true;
}



bool I2C::readByte(uint8_t regAddr,  uint8_t& byte){
	
	if(!_isSetup) return false;

	union i2c_smbus_data data;
	
	if(i2c_smbus_access (_fd, I2C_SMBUS_READ, regAddr, I2C_SMBUS_BYTE_DATA, &data) < 0){
		
		ELOG_ERROR(ErrorMgr::FAC_I2C, _devAddr, errno,  "I2C_SMBUS_READ BYTE (%02x) ", regAddr);
 
		return false;
	}

	byte = data.byte & 0xFF;
	return true;
}


bool I2C::readWord(uint8_t regAddr,  int16_t& word, bool swap){
	if(!_isSetup) return false;

	union i2c_smbus_data data;
	
	if(i2c_smbus_access (_fd, I2C_SMBUS_READ, regAddr, I2C_SMBUS_WORD_DATA, &data) < 0){
		
		ELOG_ERROR(ErrorMgr::FAC_I2C, _devAddr, errno,  "I2C_SMBUS_READ WORD (%02x) ", regAddr);

		return false;
	}

	if(swap){
		word = ((data.block[0]) << 8) | (data.block[1] );
	}
	else {
		word = data.word;
	}
	return true;

}

bool I2C::readWord(uint8_t regAddr,  uint16_t& word, bool swap){

	if(!_isSetup) return false;

	union i2c_smbus_data data;
	
	if(i2c_smbus_access (_fd, I2C_SMBUS_READ, regAddr, I2C_SMBUS_WORD_DATA, &data) < 0){
		
		ELOG_ERROR(ErrorMgr::FAC_I2C, _devAddr, errno,  "I2C_SMBUS_READ WORD (%02x) ", regAddr);

		return false;
	}

	if(swap){
		word = ((data.block[0]) << 8) | (data.block[1] );
	}
	else {
		word = data.word;
	}
	return true;
}
 
/*
 
 i2cdetect -F 1
 Functionalities implemented by /dev/i2c-1:
 I2C                              yes
 SMBus Quick Command              yes
 SMBus Send Byte                  yes
 SMBus Receive Byte               yes
 SMBus Write Byte                 yes
 SMBus Read Byte                  yes
 SMBus Write Word                 yes
 SMBus Read Word                  yes
 SMBus Process Call               yes
 SMBus Block Write                yes
 SMBus Block Read                 no		<<<----
 SMBus Block Process Call         no
 SMBus PEC                        yes
 I2C Block Write                  yes
 I2C Block Read                   yes

 */

bool I2C::readBlock(uint8_t regAddr, uint8_t size, i2c_block_t & block ){

	if(!_isSetup) return false;

	union i2c_smbus_data data;

	memset(data.block, 0, sizeof(data.block));
#if 0
	data.block[0] = size + 1;

	if(i2c_smbus_access (_fd, I2C_SMBUS_READ, regAddr, I2C_SMBUS_I2C_BLOCK_DATA, &data) < 0){
		
		ELOG_ERROR(ErrorMgr::FAC_I2C, _devAddr, errno,  "I2C_SMBUS_READ BLOCK (%02x) ", regAddr);

		return false;
	}
	memcpy(block, data.block, sizeof(block));
	return true;

#else
	
	bool status = false;
	
	if(size > sizeof(block))
		return false;
	
	status = readByte(regAddr, block[0]);
	if(status) {
		for(int i = 1; i < size; i++){
			status &= readByte( block[i]);
			if(!status) break;
		}
	}
	
	if(status)
		memcpy(block, data.block, sizeof(block));

	return status;
	
#endif
	
}

bool I2C::writeBlock(uint8_t regAddr, uint8_t size, i2c_block_t  block ){

	if(!_isSetup) return false;

	union i2c_smbus_data data;

	memset(data.block, 0, sizeof(data.block));
	
	if (size > 32)
		size = 32;
	
	for (int i = 1; i <= size; i++)
		 data.block[i] = block[i-1];
	data.block[0] = size;

	if(i2c_smbus_access (_fd, I2C_SMBUS_WRITE, regAddr, I2C_SMBUS_I2C_BLOCK_DATA, &data) < 0){
		
		ELOG_ERROR(ErrorMgr::FAC_I2C, _devAddr, errno,  "I2C_SMBUS_WRITE BLOCK (%02x) ", regAddr);

		return false;
	}
 
	return true;
}


 
