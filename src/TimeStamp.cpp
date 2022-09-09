//
//  TimeStamp.cpp

//
//  Created by Vincent Moscaritolo on 5/6/21.
//

#include "TimeStamp.hpp"
#include <time.h>

using namespace std;
 
namespace timestamp {
	static const char *kDateFormat = "%a, %d %b %Y %T GMT";

static const char *kISO8601Format = "%F %T";

	static const char *kLogFormat = "%d.%m.%y %T";
	static const char *kClockFormat = "%l:%M %p";


	TimeStamp::TimeStamp(bool isGMT){
		struct tm timeinfo = {0};
		_time = time(NULL);

		if(!isGMT){
			localtime_r(&_time, &timeinfo);
			_time += timeinfo.tm_gmtoff;
		}
 
	}

	TimeStamp::TimeStamp(const string str){
		struct tm tm;
		if(::strptime( str.c_str(), kDateFormat, &tm)){
			_time = ::timegm(&tm);
		}
	}

	std::string TimeStamp::RFC1123String(){
		enum { RFC1123_GMT_LEN = 29, RFC1123_GMT_SIZE };
		char timeStr[RFC1123_GMT_SIZE] = {0};
		::strftime(timeStr, sizeof(timeStr), kDateFormat,  gmtime(&_time));
		return string(timeStr);
	}

std::string TimeStamp::ISO8601String(){
	enum { ISO8601_GMT_LEN = 30, ISO8601_GMT_SIZE };
	char timeStr[ISO8601_GMT_SIZE] = {0};
	::strftime(timeStr, sizeof(timeStr), kISO8601Format,  gmtime(&_time));
	return string(timeStr);
}

 
std::string TimeStamp::logFileString(){
	enum { RFC1123_GMT_LEN = 29, RFC1123_GMT_SIZE };
	char timeStr[RFC1123_GMT_SIZE] = {0};
	::strftime(timeStr, sizeof(timeStr), kLogFormat,  gmtime(&_time));
	return string(timeStr);
}


	std::string TimeStamp::ClockString(bool isGMT){
		char timeStr[80] = {0};
		
		time_t rawtime;
		struct tm timeinfo = {0};

		time(&rawtime);
		localtime_r(&rawtime, &timeinfo); // fills in your structure,
													 // instead of returning a pointer to a static
 
 		if(isGMT)
			gmtime_r(&_time, &timeinfo);
		else
			localtime_r(&_time, &timeinfo);

		::strftime(timeStr, sizeof(timeStr), kClockFormat, &timeinfo );
		return(string(timeStr));
	}

}
 
