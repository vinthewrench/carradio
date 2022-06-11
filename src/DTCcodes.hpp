//
//  DTCcodes.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 6/11/22.
//

#pragma once

#include <map>
#include <string>

#include <sqlite3.h>

#include "CommonDefs.hpp"
 
using namespace std;
 
class DTCcodes {

public:

	DTCcodes();
	~DTCcodes();

	bool descriptionForDTCCode(string code, string& description);
	void flushCache();
	
private:
	sqlite3 	*_sdb;

	map<string, string> 		_cachedCodes;

};
