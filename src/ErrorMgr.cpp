//
//  ErrorMgr.cpp
//  coopserver
//
//  Created by Vincent Moscaritolo on 2/27/22.
//

#include "ErrorMgr.hpp"

ErrorMgr *ErrorMgr::sharedInstance = NULL;


ErrorMgr::ErrorMgr(){
	
}

ErrorMgr::~ErrorMgr(){
	
}

  

void ErrorMgr::logError( level_t 	level,
								facility_t 	facility,
								uint8_t 		deviceID,
								int  			errnum,
								const char *format, ...){
	
	
	char *s;
	va_list args;
	va_start(args, format);
	vasprintf(&s, format, args);

	string errorMsg;
	
	if(errnum > 0){
		errorMsg = string(strerror(errnum));
	}
	basic_string <char> str(s, strlen(s));
	
	printf("Error %s %s \n", str.c_str(), errorMsg.c_str());
	free(s);
	va_end(args);
}



