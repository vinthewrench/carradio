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


#include "CommonDefs.hpp"

using namespace std;

class PiCanDB  {
 
	public:
	
	PiCanDB ();
	~PiCanDB ();

	// MARK: - properties
	bool savePropertiesToFile(string filePath = "") ;
	bool restorePropertiesFromFile(string filePath = "");
 
	bool setProperty(string key, string value);
	bool getProperty(string key, string *value);
	bool setPropertyIfNone(string key, string value);

	bool getUint16Property(string key, uint16_t * value);
	bool getFloatProperty(string key, float * valOut);
	bool getBoolProperty(string key, bool * valOut);

	bool removeProperty(string key);
	map<string ,string> getProperties();
	
	// MARK: - values
	void updateValues(map<string,string>  values, time_t when = 0);
	void updateValue(string key, string value, time_t when);
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

private:
	
	string defaultPropertyFilePath();

	mutable std::mutex _mutex;

	map<string,string> 			_properties;
	string 							_propertyFilePath;
  
	// value database
	eTag_t 		_lastEtag;

	typedef struct {
		time_t			lastUpdate;
		eTag_t 			eTag;
		string			value;
		} value_t;

	map<string, value_t> _values;
};
