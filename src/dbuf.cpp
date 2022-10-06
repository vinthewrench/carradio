//
//  dbuf.cpp
//
//  Created by Vincent Moscaritolo on 10/4/22.
//

#include "dbuf.hpp"
#include <string.h>
#include <stdlib.h>

// MARK: -  Dbuf

#define ALLOC_QUANTUM 16


dbuf::dbuf(){
	_used = 0;
	_pos = 0;
	_alloc = ALLOC_QUANTUM;
	_data =  (uint8_t*) malloc(ALLOC_QUANTUM);
 }

dbuf::~dbuf(){
	if(_data)
		free(_data);
	_data = NULL;
	_used = 0;
	_pos = 0;
	_alloc = 0;
 }

bool dbuf::append_data(void* data, size_t len){
	
	if(len + _pos  >  _alloc){
		size_t newSize = len + _used + ALLOC_QUANTUM;
		
		if( (_data = (uint8_t*) realloc(_data,newSize)) == NULL)
			return false;
		
		_alloc = newSize;
	}
	
	 if(_pos < _used) {
		 memmove((void*) (_data + _pos + len) ,
					(_data + _pos),
					_used -_pos);
	 }
	 
	memcpy((void*) (_data + _pos), data, len);
	 
	 _pos += len;
	 _used += len;
 //	if(_pos > _used)
 //		_used = _pos;
	 
	return true;
}


bool dbuf::reserve(size_t len){
	
	if(len + _pos  >  _alloc){
		size_t newSize = len + _used + ALLOC_QUANTUM;
		
		if( (_data = (uint8_t*) realloc(_data,newSize)) == NULL)
			return false;
		
		_alloc = newSize;
	}

	if(_pos < _used) {
		memmove((void*) (_data + _pos + len) ,
				  (_data + _pos),
				  _used -_pos);
	}
	 
	return true;
}

