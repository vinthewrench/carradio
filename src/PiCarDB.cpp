//
//  RadDB.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/8/22.
//

#include "PiCarDB.hpp"
 
#include <stdlib.h>
#include <algorithm>
#include <regex>
#include <limits.h>
#include <iostream>
#include <filesystem> // C++17
#include <fstream>
#include <time.h>
#include "json.hpp"
#include "ErrorMgr.hpp"
#include "PropValKeys.hpp"

using namespace nlohmann;

PiCarDB::PiCarDB (){
	_lastEtag = 0;
	_values.clear();
//	_properties.clear();
	_props.clear();
	
	_didChangeProperties  = false;
}

PiCarDB::~PiCarDB (){
	
}


void  PiCarDB::clearValues(){
	_values.clear();
	_lastEtag = 0;
}

int PiCarDB::valuesCount() {
	return (int) _values.size();
}



void  PiCarDB::updateValues(map<string,string>  values, time_t when){
	
	if(when == 0)
		when = time(NULL);
	
	for (auto& [key, value] : values)
		updateValue(key, value, when );
		
 };

 
void PiCarDB::updateValue(string key, string value, time_t when){
	
	std::lock_guard<std::mutex> lock(_mutex);

	if(when == 0)
		when = time(NULL);

	bool shouldUpdate = true;
	
	// filter out noise.
	if(_values.count(key)){
		auto lastValue = _values[key];
		
		if( lastValue.value == value)
			shouldUpdate = false;
	}
	
	if(shouldUpdate)
		_values[key] = {when, _lastEtag++, value};
}




void PiCarDB::updateValue(string key, uint32_t value, time_t  when){
	updateValue(key, to_string(value), when);
}

void PiCarDB::updateValue(string key, int value, time_t  when){
	updateValue(key, to_string(value), when);
}


void PiCarDB::updateValue(string key, float value, time_t  when){
	updateValue(key, to_string(value), when);
}


void PiCarDB::updateValue(string key, double value, time_t  when){
	updateValue(key, to_string(value), when);
}

void PiCarDB::updateValue(string key, bool value, time_t  when){
	updateValue(key, to_string(value), when);
}
 
vector<string> PiCarDB::allValueKeys(){
	std::lock_guard<std::mutex> lock(_mutex);

	vector<string> keys;
	keys.clear();
	
	for (const auto& [key, value] : _values) {
			keys.push_back(key);
	}

	return keys;
}
  

vector<string> PiCarDB::valuesUpdateSinceEtag(eTag_t eTag, eTag_t *eTagOut){
	
	std::lock_guard<std::mutex> lock(_mutex);
	vector<string> keys = {};
	
	for (const auto& [key, value] : _values) {
		if(value.eTag <= eTag)
			keys.push_back(key);
	}

	if(eTagOut)
		*eTagOut = _lastEtag;

	return keys;
};

vector<string> PiCarDB::valuesOlderthan(time_t time){
	
	std::lock_guard<std::mutex> lock(_mutex);
	vector<string> keys = {};
	
	for (const auto& [key, value] : _values) {
		if(value.lastUpdate < time)
			keys.push_back(key);
	}

	return keys;
};


bool PiCarDB::valueWithKey(string key, string &valueOut){
	std::lock_guard<std::mutex> lock(_mutex);
	
	if(_values.count(key) == 0 )
		return false;

	valueOut = _values[key].value;
	return true;
};

bool PiCarDB::getStringValue(string key,  string &result){
	return valueWithKey(key,result);
}

bool PiCarDB::getFloatValue(string key,  float &result){
	
	string str;
	if(valueWithKey(key,str)) {
		char* p;
		float val = strtof(str.c_str(), &p);
		if(*p == 0){
			result = val;
			return true;
		}
	}
	return false;
}

bool PiCarDB::getDoubleValue(string key,  double &result){
	
	string str;
	if(valueWithKey(key,str)) {
		char* p;
		double val = strtod(str.c_str(), &p);
		if(*p == 0){
			result = val;
			return true;
		}
	}
	return false;
}

bool PiCarDB::getBoolValue(string key,  bool &result){
	
	bool valid = false;
	
	string str;
	if(valueWithKey(key,str)) {
		
		if(!empty(str)){
			const char * param1 = str.c_str();
			int intValue = atoi(param1);
			
			// check for level
			if(std::regex_match(param1, std::regex("^[0-1]$"))){
				result = bool(intValue);
				valid = true;
			}
			else {
				if(caseInSensStringCompare(str,"off")) {
					result = false;
					valid = true;
				}
				else if(caseInSensStringCompare(str,"on")) {
					result = true;
					valid = true;
				}
			}
		}
		
	}
 
	return valid;
	
}

 
bool PiCarDB::getIntValue(string key,  int &result) {
	
	string str;
	if(valueWithKey(key,str)) {
		char* p;
		long val = strtol(str.c_str(), &p, 0);
		if(*p == 0){
			result = (int) val;
			return true;
		}
	}
	return false;
}


bool PiCarDB::getUInt32Value(string key,  uint32_t &result){
	string str;
	if(valueWithKey(key,str)) {
		char* p;
		unsigned long val = strtoul(str.c_str(), &p, 0);
		if(*p == 0 && val < UINT32_MAX ){
			result = (uint32_t) val;
			return true;
		}
	}
	return false;
}


// MARK: - properties
bool PiCarDB::setProperty(string key, string value){
	
	bool shouldUpdate =
			(_props.count(key) == 0)
		 ||(_props[key] != value) ;
	
	if(shouldUpdate) {
		_props[key] = value;
		_didChangeProperties  = shouldUpdate;
 	}
 
	return true;
}

bool PiCarDB::removeProperty(string key){
	
	if(_props.count(key)){
		_props.erase(key);
		savePropertiesToFile();
	 
		return true;
	}
	return false;
}

bool PiCarDB::setPropertyIfNone(string key, string value){
	
	if(_props.count(key) == 0){
		_props[key] = value;
		savePropertiesToFile();
		return true;
	}
	return false;
}
 

vector<string> PiCarDB::propertiesKeys(){
	
	vector<string> keys = {};
	for(auto it =  _props.begin(); it != _props.end(); ++it) {
		keys.push_back(it.key());
	}
	return keys;
}



bool PiCarDB::setProperty(string key, nlohmann::json  value){
	
	bool shouldUpdate =
			(_props.count(key) == 0)
		 ||(_props[key] != value) ;
	
	if(shouldUpdate) {
		_props[key] = value;
		_didChangeProperties  = shouldUpdate;
	}
	
	return true;
}


bool PiCarDB::getProperty(string key, string *value){
	
	if( _props.contains(key)
		&&  _props.at(key).is_string())
	{
		if(value)
			*value  = _props.at(key);
		return true;
 	}

	return false;
}

bool PiCarDB::getTimeProperty(string key, time_t * valOut){
	if( _props.contains(key)
		&&  _props.at(key).is_number_unsigned())
	{
		auto val = _props.at(key);
		
		if(valOut)
			*valOut = (time_t) val;
		return true;
		
	}
	return false;
}



bool  PiCarDB::getUint16Property(string key, uint16_t * valOut){
	
	if( _props.contains(key)
		&&  _props.at(key).is_number_unsigned())
	{
		auto val = _props.at(key);
		
		if(val <= UINT16_MAX){
			if(valOut)
				*valOut = (uint16_t) val;
			return true;
		}
	}
	return false;
}

bool  PiCarDB::getFloatProperty(string key, float * valOut){
	
	if( _props.contains(key)
		&&  _props.at(key).is_number_float())
	{
		auto val = _props.at(key);
		if(valOut)
				*valOut = (uint16_t) val;
			return true;
		}
   	return false;
}
 
bool  PiCarDB::getBoolProperty(string key, bool * valOut){
	
	if( _props.contains(key) ){
	 	auto val = _props.at(key);

		if(_props.at(key).is_boolean()){
 			if(valOut) *valOut = (bool)val;
			return true;
		}
		else 	if(_props.at(key).is_string()){
			if(val == "true"){
				if(valOut) *valOut = true;
				return true;
			}
			else if(val == "false"){
				if(valOut)	*valOut = false;
				return true;
			}
		}
	}
 	return false;
}



bool PiCarDB::getJSONProperty(string key, nlohmann::json  *valOut){
	
	if( _props.contains(key)
		&& ( _props.at(key).is_object() ||  _props.at(key).is_array()) )
	{
		auto val = _props.at(key);
		if(valOut)
				*valOut = val;
			return true;
		}
 
	return  false;
}
 

//MARK: - Database Persistent operations

bool PiCarDB::restorePropertiesFromFile(string filePath){

	std::ifstream	ifs;
	bool 				statusOk = false;

	if(filePath.empty())
		filePath = _propertyFilePath;

	if(filePath.empty())
		filePath = defaultPropertyFilePath();
	
	try{
		string line;
		std::lock_guard<std::mutex> lock(_mutex);
	
		// open the file
		ifs.open(filePath, ios::in);
		if(!ifs.is_open()) return false;
	
		_props.clear();
		
		json jP;
		ifs >> _props;
		
		statusOk = true;
		_didChangeProperties  = false;

		ifs.close();
		
		// if we were sucessful, then save the filPath
		_propertyFilePath	= filePath;
	}
	catch(std::ifstream::failure &err) {
		ELOG_MESSAGE("restorePropertiesFromFile:FAIL: %s", err.what());
		statusOk = false;
	}
	
	return statusOk;
}

 
bool PiCarDB::savePropertiesToFile(string filePath){
 
	std::lock_guard<std::mutex> lock(_mutex);
	bool statusOk = false;
	
	time_t now = time(NULL);

	_props[ PROP_LAST_WRITE_DATE] = now;
	
	std::ofstream ofs;
	
	if(filePath.empty())
		filePath = _propertyFilePath;

	if(filePath.empty())
		filePath = defaultPropertyFilePath();

	try{
		ofs.open(filePath, std::ios_base::trunc);
		
		if(ofs.fail())
			return false;

		string jsonStr = _props.dump(4);
		ofs << jsonStr << "\n";
		
		ofs.flush();
		ofs.close();
			
		statusOk = true;
		_didChangeProperties  = false;

	}
	catch(std::ofstream::failure &writeErr) {
			statusOk = false;
	}

		
	return statusOk;
}

string PiCarDB::defaultPropertyFilePath(){
	return "carradio.props.json";
}
