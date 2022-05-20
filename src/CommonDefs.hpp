//
//  CommonDefs.h
//  vfdtest
//
//  Created by Vincent Moscaritolo on 4/13/22.
//
#pragma once


#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <functional>
#include <vector>
#include <string>
#include <exception>
#include <stdexcept>
#include <type_traits>

#include "Utils.hpp"


typedef uint64_t eTag_t;
#define MAX_ETAG UINT64_MAX

#ifndef _UINT16_T
#define _UINT16_T
typedef unsigned short uint16_t;
#endif /* _UINT16_T */

typedef std::function<void(bool didSucceed)> boolCallback_t;
typedef std::function<void()> voidCallback_t;
typedef std::vector<std::string> stringvector;


//// did you know C++ changed the moduo operator to allow negative numbers?  ... WTF!!
inline int mod(int a, int b)
{
	if(b < 0) //you can check for b == 0 separately and do what you want
	  return -mod(-a, -b);
	int ret = a % b;
	if(ret < 0)
	  ret+=b;
	return ret;
}
 
class Exception: virtual public std::runtime_error {
	
protected:
	
	int error_number;               ///< Error Number
	unsigned line;						// line number
	const char* function	; 			//function name
public:
	
	/** Constructor (C++ STL string, int, int).
	 *  @param msg The error message
	 *  @param err_num Error number
	 */
	explicit Exception(const std::string& msg, int err_num = 0):
	std::runtime_error(msg)
	{
		line = __LINE__;
		function = __FUNCTION__;
		error_number = err_num;
	}
	
	
	/** Destructor.
	 *  Virtual to allow for subclassing.
	 */
	virtual ~Exception() throw () {}
	
	/** Returns error number.
	 *  @return #error_number
	 */
	virtual int getErrorNumber() const throw() {
		return error_number;
	}
};
 
