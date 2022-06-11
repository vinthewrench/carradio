//
//  DTCcodes.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 6/11/22.
//

#include "DTCcodes.hpp"
#include "ErrorMgr.hpp"



DTCcodes::DTCcodes(){
	_sdb = NULL;
	_cachedCodes.clear();

 }

DTCcodes::~DTCcodes(){
	
	flushCache();
}
 
void DTCcodes::flushCache(){
	
	_cachedCodes.clear();

	if(_sdb)
	{
		sqlite3_close(_sdb);
		_sdb = NULL;
	}
}

bool DTCcodes::descriptionForDTCCode(string code, string& description){
	
	bool success = false;
	
	if(_cachedCodes.count(code)){
		description = _cachedCodes[code];
		success = true;
	}
	else {
		
		// lazy open DB
		if(!_sdb){
			string filePath = "file:DTC.db?mode=ro";
		 
			if(sqlite3_open(filePath.c_str(), &_sdb) != SQLITE_OK){
				ELOG_ERROR(ErrorMgr::FAC_CAN, 0, 0,  "sqlite3_open FAILED: %s %s",filePath.c_str(), sqlite3_errmsg(_sdb	) );
				return false;
			}
 		}
		sqlite3_stmt* stmt = NULL;

		string sql = string("SELECT DESCRIPTION FROM CODES WHERE CODE = \"" + code + "\" LIMIT 1;");
		sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL);
		
		while ( (sqlite3_step(stmt)) == SQLITE_ROW) {
			if(sqlite3_column_type(stmt,0) != SQLITE_NULL){
				
				description = string((char*) sqlite3_column_text(stmt, 0));
				_cachedCodes[code] = description;
				success = true;
				break;
			}
		}
		sqlite3_finalize(stmt);
	}
	
	return success;
}
