//
//  RadDB.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/8/22.
//

#pragma once


#include <vector>
#include <map>
#include <algorithm>
#include <mutex>
#include <bitset>
#include <strings.h>
#include <cstring>

#include "json.hpp"

#include "CommonDefs.hpp"
 
using namespace std;


class PiCarDB  {
 
	public:
	
	PiCarDB ();
	~PiCarDB ();

	// MARK: - properties // persisant
	bool savePropertiesToFile(string filePath = "") ;
	bool restorePropertiesFromFile(string filePath = "");
 
	bool setProperty(string key, string value);
	bool setProperty(string key, nlohmann::json  j);
	bool getProperty(string key, string *value);
	
	bool setPropertyIfNone(string key, string value);

	bool getUint16Property(string key, uint16_t * value);
	bool getFloatProperty(string key, float * valOut);
	bool getBoolProperty(string key, bool * valOut);
	bool getJSONProperty(string key, nlohmann::json  *j);
	
	bool removeProperty(string key);
	vector<string> propertiesKeys();
	
	bool propertiesChanged() {return _didChangeProperties;};
	
 
	// MARK: - values
	void updateValues(map<string,string>  values, time_t when = 0);
	void updateValue(string key, string value, time_t when);
	
	void updateValue(string key, bool value, time_t  when = 0);
	void updateValue(string key, int value, time_t  when = 0);
	void updateValue(string key, float value, time_t  when = 0);
	void updateValue(string key, double value, time_t  when = 0);
	void updateValue(string key, uint32_t value, time_t   when = 0);

	void clearValues();
	int valuesCount();

	vector<string> 	allValueKeys();
	vector<string>  	valuesUpdateSinceEtag(eTag_t eTag, eTag_t *newEtag);
	vector<string>  	valuesOlderthan(time_t time);
	
	bool valueWithKey	 (string key, string  &valueOut);
	bool getStringValue(string key,	string &result);
	bool getFloatValue(string key,  float &result);
	bool getDoubleValue(string key,  double &result);
	bool getIntValue(string key,  int &result);
	bool getUInt32Value(string key,  uint32_t &result);
	bool getBoolValue(string key,  bool &result);

private:
	
	string defaultPropertyFilePath();

	mutable std::mutex _mutex;

	nlohmann::json					_props;
	
	string 							_propertyFilePath;
	bool								_didChangeProperties;
	
	
	// value database
	eTag_t 		_lastEtag;

	typedef struct {
		time_t			lastUpdate;
		eTag_t 			eTag;
		string			value;
		} value_t;

	map<string, value_t> _values;
};
