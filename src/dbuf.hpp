//
//  dbuf.h
//  spmetadata
//
//  Created by Vincent Moscaritolo on 10/4/22.
//


#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

using namespace  std;

 
class dbuf{
	
public:
	dbuf();
	~dbuf();

	bool append_data(void* data, size_t len);

	bool reserve(size_t len);

	inline bool   append_char(uint8_t c){
	  return append_data(&c, 1);
	}
	
	inline  void reset(){ _pos = 0; _used = 0; };

	size_t size() { return _used;};
	uint8_t *data () { return _data;};
	
private:
	
	size_t  	_pos;			// cursor pos
	size_t  	_used;			// actual end of buffer
	size_t  	_alloc;
	uint8_t*  _data;

};
