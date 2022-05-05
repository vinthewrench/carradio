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
 
