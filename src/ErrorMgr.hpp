//
//  ErrorMgr.hpp
//  coopserver
//
//  Created by Vincent Moscaritolo on 2/27/22.
//

#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <string>
 
 
#define ELOG_MESSAGE( _msg_, ...)  \
		ErrorMgr::shared()->logError(ErrorMgr::LEV_MESSAGE,  ErrorMgr::UNKNOWN, 0 ,0, _msg_, ##__VA_ARGS__)

#define ELOG_ERROR(_fac_, _dev_, _errnum_, _msg_, ...)  \
	  ErrorMgr::shared()->logError(ErrorMgr::LEV_ERROR, _fac_, _dev_, _errnum_, _msg_, ##__VA_ARGS__)

 
using namespace std;


class ErrorMgr {
public:

	typedef enum {
		LEV_MESSAGE,
		LEV_DEBUG,
		LEV_INFO,
		LEV_WARNING,
 		LEV_ERROR
	}level_t;
	 
	typedef enum {
		FAC_UNKNOWN,
		FAC_I2C,
		FAC_GPIO,
		FAC_POWER,
		FAC_SENSOR,
		FAC_DEVICE,
		FAC_AUDIO,
		FAC_RTL,
	 	}facility_t;
	
	
	static ErrorMgr *shared() {
		if(!sharedInstance){
			sharedInstance = new ErrorMgr;
		}
		return sharedInstance;
	}
	
	ErrorMgr();
	~ErrorMgr();
	
	void logError( level_t 		level,
					  facility_t 	facility,
					  uint8_t 		deviceID,
					  int  			errnum,
					  const char *format, ... );
	

private:
	
	static ErrorMgr *sharedInstance;

};

