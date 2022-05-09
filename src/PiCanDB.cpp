//
//  RadDB.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/8/22.
//

#include "PiCanDB.hpp"
 
#include <stdlib.h>
#include <algorithm>
#include <regex>
#include <limits.h>
#include <iostream>
#include <filesystem> // C++17
#include <fstream>
#include "json.hpp"
#include "ErrorMgr.hpp"

using namespace nlohmann;

PiCanDB::PiCanDB (){
	_lastEtag = 0;
	_values.clear();
}

PiCanDB::~PiCanDB (){
	
}


void  PiCanDB::clearValues(){
	_values.clear();
	_lastEtag = 0;
}

int PiCanDB::valuesCount() {
	return (int) _values.size();
}



void  PiCanDB::updateValues(map<string,string>  values, time_t when){
	
	if(when == 0)
		when = time(NULL);
	
	for (auto& [key, value] : values)
		updateValue(key, value, when );
		
 };

 
void PiCanDB::updateValue(string key, string value, time_t when){
	
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



vector<string> PiCanDB::allValueKeys(){
	std::lock_guard<std::mutex> lock(_mutex);

	vector<string> keys;
	keys.clear();
	
	for (const auto& [key, value] : _values) {
			keys.push_back(key);
	}

	return keys;
}
  

vector<string> PiCanDB::valuesUpdateSinceEtag(eTag_t eTag, eTag_t *eTagOut){
	
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

vector<string> PiCanDB::valuesOlderthan(time_t time){
	
	std::lock_guard<std::mutex> lock(_mutex);
	vector<string> keys = {};
	
	for (const auto& [key, value] : _values) {
		if(value.lastUpdate < time)
			keys.push_back(key);
	}

	return keys;
};


bool PiCanDB::valueWithKey(string key, string &valueOut){
	std::lock_guard<std::mutex> lock(_mutex);
	
	if(_values.count(key) == 0 )
		return false;

	valueOut = _values[key].value;
	return true;
};

bool PiCanDB::getStringValue(string key,  string &result){
	return valueWithKey(key,result);
}

bool PiCanDB::getFloatValue(string key,  float &result){
	
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

bool PiCanDB::getDoubleValue(string key,  double &result){
	
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

 
bool PiCanDB::getIntValue(string key,  int &result) {
	
	string str;
	if(valueWithKey(key,str)) {
		char* p;
		long val = strtoul(str.c_str(), &p, 0);
		if(*p == 0){
			result = (uint16_t) val;
			return true;
		}
	}
	return false;
}


// MARK: - properties
bool PiCanDB::setProperty(string key, string value){
	_properties[key] = value;
	savePropertiesToFile();
	
	 
	return true;
}

bool PiCanDB::removeProperty(string key){
	
	if(_properties.count(key)){
		_properties.erase(key);
		savePropertiesToFile();
	 
		return true;
	}
	return false;
}

bool PiCanDB::setPropertyIfNone(string key, string value){
	
	if(_properties.count(key) == 0){
		_properties[key] = value;
		savePropertiesToFile();
		return true;
	}
	return false;
}

map<string ,string> PiCanDB::getProperties(){
	
	return _properties;
}

bool PiCanDB::getProperty(string key, string *value){
	
	if(_properties.count(key)){
		if(value)
			*value = _properties[key];
		return true;
	}
	return false;
}

bool  PiCanDB::getUint16Property(string key, uint16_t * valOut){
	
	string str;
	if(getProperty(string(key), &str)){
		char* p;
		long val = strtoul(str.c_str(), &p, 0);
		if(*p == 0 && val <= UINT16_MAX){
			if(valOut)
				*valOut = (uint16_t) val;
			return true;
		}
	}
	return false;
}

bool  PiCanDB::getFloatProperty(string key, float * valOut){
	
	string str;
	if(getProperty(string(key), &str)){
		char* p;
		float val = strtof(str.c_str(), &p);
		if(*p == 0){
			if(valOut)
				*valOut = (float) val;
			return true;
		}
	}
	return false;
}
 
bool  PiCanDB::getBoolProperty(string key, bool * valOut){
	
	string str;
	if(getProperty(string(key), &str) ){
		char* p;
		
		transform(str.begin(), str.end(), str.begin(), ::tolower);
		
		long val = strtoul(str.c_str(), &p, 0);
		if(*p == 0 && (val == 0 || val == 1)){
			if(valOut) *valOut = (bool)val;
			return true;
			
		}else if(str == "true"){
			if(valOut) *valOut = true;
			return true;
		}
		else if(str == "false"){
			if(valOut)	*valOut = false;
			return true;
		}
	}
	return false;
}


//MARK: - Database Persistent operations

bool PiCanDB::restorePropertiesFromFile(string filePath){

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
	
		json jP;
		ifs >> jP;
		
		for (json::iterator it = jP.begin(); it != jP.end(); ++it) {
			
		 if( it.value().is_string()){
				_properties[it.key() ] = string(it.value());
			}
			else if (it.value().is_number()){
				_properties[it.key() ] = to_string(it.value());
			}
			else if (it.value().is_boolean()){
				_properties[it.key() ] = to_string(it.value());
			}
		}
		statusOk = true;
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

 
bool PiCanDB::savePropertiesToFile(string filePath){
 
	std::lock_guard<std::mutex> lock(_mutex);
	bool statusOk = false;
	
	std::ofstream ofs;
	
	if(filePath.empty())
		filePath = _propertyFilePath;

	if(filePath.empty())
		filePath = defaultPropertyFilePath();

	try{
		ofs.open(filePath, std::ios_base::trunc);
		
		if(ofs.fail())
			return false;

		json jP;

		for (auto& [key, value] : _properties) {
			jP[key] =  string(value);
		}
		
		string jsonStr = jP.dump(4);
		ofs << jsonStr << "\n";
		
		ofs.flush();
		ofs.close();
			
		statusOk = true;
	}
	catch(std::ofstream::failure &writeErr) {
			statusOk = false;
	}

		
	return statusOk;
}

string PiCanDB::defaultPropertyFilePath(){
	return "carradio.props.json";
}
