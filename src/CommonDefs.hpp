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

#define DEBUG_THREADS 1
#define USE_COMPASS 0

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
 
template <class T>
class ClassName
{
public:
  static std::string Get()
  {
	 // Get function name, which is "ClassName<class T>::Get"
	 // The template parameter 'T' is the class name we're looking for
	 std::string name = __FUNCTION__;
	 // Remove "ClassName<class " ("<class " is 7 characters long)
	 size_t pos = name.find_first_of('<');
	 if (pos != std::string::npos)
		name = name.substr(pos + 7);
	 // Remove ">::Get"
	 pos = name.find_last_of('>');
	 if (pos != std::string::npos)
		name = name.substr(0, pos);
	 return name;
  }
};

template <class T>
std::string GetClassName(const T* _this = NULL)
{
  return ClassName<T>::Get();
}

#if DEBUG_THREADS & !defined(__APPLE__)
#include <sys/syscall.h>
#define  PRINT_CLASS_TID  printf("%5ld %s\n", (long) syscall(SYS_gettid), __FUNCTION__)
#else
#define  PRINT_CLASS_TID
#endif
