//
//  ErrorMgr.cpp
//  coopserver
//
//  Created by Vincent Moscaritolo on 2/27/22.
//

#include "ErrorMgr.hpp"

#include "TimeStamp.hpp"

using namespace std;
using namespace timestamp;

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


void ErrorMgr::logMessage(level_t level, bool logTime, const char *format, ...){

	char *s;
	va_list args;
	va_start(args, format);
	vasprintf(&s, format, args);

 	basic_string <char> str(s, strlen(s));

	logMessage_str(level,logTime, s);
	free(s);
	va_end(args);
}


void ErrorMgr::logMessage_str(level_t level, bool logTime, string str){
 
	//if((_logFlags & level) == 0) return;
	if(logTime)
		logTimedStampString(str);
	else
		writeToLog(str);

}


void ErrorMgr::logTimedStampString(const string  str){
	writeToLog( TimeStamp(false).logFileString() + "\t" + str + "\n");
}



void ErrorMgr::writeToLog(const std::string str){
	
	writeToLog((const uint8_t*)str.c_str(), str.length());
}

void ErrorMgr::writeToLog(const uint8_t* buf, size_t len){
	
	lock_guard<std::mutex> lock(_mutex);

	if(len){
		printf("%.*s",(int)len, buf);
   	}
}


 
