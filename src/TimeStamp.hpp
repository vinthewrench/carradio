//
//  TimeStamp.hpp

//
//  Created by Vincent Moscaritolo on 5/6/21.
//

#ifndef TimeStamp_hpp
#define TimeStamp_hpp

#include <stdlib.h>
#include <time.h>
#include <string>

namespace timestamp {

class TimeStamp{
public:

	TimeStamp(bool isGMT = true);

	TimeStamp(std::string str);
	TimeStamp(time_t time) { _time = time;};
	inline time_t getTime() { return _time; };
	std::string RFC1123String();
	std::string ISO8601String();
	std::string logFileString();

	std::string ClockString(bool isGMT = true);
	
	
private:
	
	time_t _time;
};

};
#endif /* TimeStamp_hpp */
