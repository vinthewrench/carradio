//
//  Wranger2010.cpp
//  canhacker
//
//  Created by Vincent Moscaritolo on 3/23/22.
//
#include "Wranger2010.hpp"
#include "CANBusMgr.hpp"
#include "FrameDB.hpp"

#include <map>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <string>

static map<uint, string> knownPid = {

	{ 0x142,	""},
	{ 0x1A5,	""},
	{ 0x1E1,	"Steering Angle "},
	{ 0x1E7,	""},
	{ 0x208,	"Lights control"},
	{ 0x20B,	"Key Position"},
	{ 0x20E,	"Transmission/ brake"},
	{ 0x211,	"Wheel Speed and Distance"},
	{ 0x214,	"Distance"},
	{ 0x217,	"Fuel Related"},
	{ 0x219,	"VIN Number"},
	{ 0x21B,	"Fuel level"},
	{ 0x21D,	""},
	{ 0x21E,	""},
	{ 0x21F,	""},
	{ 0x221,	""},
	{ 0x244,	"Door Status"},
	{ 0x249,	"SKREEM related "},
	{ 0x25F,	""},
	{ 0x270,	"Climate Control switch"},
	{ 0x283,	""},
	{ 0x286,	"Parking brake?"},
	{ 0x290,	""},
	{ 0x291,	"Radio Mode"},
	{ 0x292,	"Temperature + Throttle?? / voltage"},
	{ 0x293,	"Radio Station"},
	{ 0x295,	"Radio Show Title"},
	{ 0x2A8,	"Wiper switch"},
	{ 0x2B0,	"Switch Panel"},
	{ 0x2CA,	""},
	{ 0x2CE,	"RPM"},
	{ 0x2D2,	"Transfer Case / Seat Belts?"},
	{ 0x2D3,	""},
	{ 0x2D6,	""},
	{ 0x2D9,	"power on check?"},
	{ 0x2DA,	""},
	{ 0x2DB,	""},
	{ 0x2DD,	""},
	{ 0x2DE,	""},
	{ 0x2DF,	""},
	{ 0x2E1,	"Headlight Switch"},
	{ 0x2E3,	""},
	{ 0x2E5,	"Rear Wiper"},
	{ 0x2E7,	"Parking Brake"},
	{ 0x2E9,	"Set time on SKREEM"},
	{ 0x2EB,	"Temperature Related"},
	{ 0x308,	"Dimmer Switch"},
	{ 0x348,	""},
	{ 0x392,	""},
	{ 0x3A3,	"Steering wheel radio control"},
	{ 0x3B0,	""},
	{ 0x3B3,	""},
	{ 0x3D9,	"Radio Settings broadcast"},
	{ 0x3E6,	"Clock Time Display"},
	{ 0x3E9,	""},
	{ 0x402,	"heartbeat 1"},
	{ 0x414,	"heartbeat 2"},
	{ 0x416,	"heartbeat 3"},
	{ 0x43E,	"heartbeat 4"},
	{ 0x514,	""} };



typedef  enum  {
		STEERING_ANGLE,
		VEHICLE_DISTANCE,
		KEY_POSITION,
		FUEL_LEVEL,
		DOORS,
		DOORS_LOCK,
		CLOCK,
		RPM,
		VIN,
} value_keys_t;

typedef FrameDB::valueSchema_t valueSchema_t;

static map<value_keys_t,  valueSchema_t> _schemaMap = {
	{STEERING_ANGLE,		{"JK_STEERING_ANGLE",			"Steering Angle",							FrameDB::DEGREES}},
	{VEHICLE_DISTANCE,	{"JK_VEHICLE_DISTANCE",			"Vehicle Distance Driven",				FrameDB::KM}},
	{KEY_POSITION,			{"JK_KEY_POSITION",				"Ignition Key Position",				FrameDB::STRING}},
	{FUEL_LEVEL,			{"JK_FUEL_LEVEL",					"Fuel Level",								FrameDB::PERCENT}},
	{DOORS,					{"JK_DOORS",						"Door State",								FrameDB::BINARY}},
	{DOORS_LOCK,			{"JK_DOORS_LOCKED",				"Door Lock",								FrameDB::BOOL}},
	{CLOCK,					{"JK_CLOCK",						"Clock Time",								FrameDB::STRING}},
	{RPM,						{"JK_ENGINE_RPM",					"Engine RPM",								FrameDB::RPM}},
	{VIN,						{"JK_VIN",							"Vehicle Identification Number",		FrameDB::STRING}},
	};


Wranger2010::Wranger2010(){
	reset();
}

void Wranger2010::reset(){
	_VIN.clear();
}

void Wranger2010::registerSchema(CANBusMgr* cbMgr){
	
	FrameDB* frameDB = cbMgr->frameDB();
	
	for (auto it = _schemaMap.begin(); it != _schemaMap.end(); it++){
		valueSchema_t*  schema = &it->second;
		frameDB->addSchema(schema->title,  {schema->title, schema->description, schema->units});
	}
}

string_view Wranger2010::schemaKeyForValueKey(int valueKey) {
	 
	auto key = static_cast<value_keys_t>( valueKey);
	valueSchema_t*  schema = &_schemaMap[key];
	return schema->title;
  }

void Wranger2010::processFrame(FrameDB* db,string ifName, can_frame_t frame, time_t when){
	switch(frame.can_id) {
			
		case 0x1E1:
		{
			uint16_t xx = (frame.data[2] <<8 | frame.data[3]);
			if (xx != 0xFFFF){
				float angle = xx - 4096. ;
				angle = angle * 0.4;
				db->updateValue(schemaKeyForValueKey(STEERING_ANGLE), to_string((int)angle), when);
			};
			
 	 		}
			break;

		case 0x20B:		//"Key Position"
		{
			string value;
			uint8_t pos =  frame.data[0];
			switch (pos) {
				case 0x00:
					value = "No Key";
					break;

				case 0x01:
					value = "OFF";
					break;

				case 0x61:
					value = "ACC";
					break;

				case 0x81:
					value = "RUN";
					break;
					
				case 0xA1:
					value = "START";
					break;

				default:
 					break;
			}
			
			if(!value.empty()){
				db->updateValue(schemaKeyForValueKey(KEY_POSITION), value, when);
			}
		}
			break;
			
		case 0x214:	//Distance
		{
			uint32_t dist = 	(frame.data[0] << 16  | frame.data[1] <<8  | frame.data[2] );
			if(dist != 0xffffff)
				db->updateValue(schemaKeyForValueKey(VEHICLE_DISTANCE), to_string(dist), when);
		}
			break;

		case 0x21B:	//Fuel level
		{
			float level = 	( (frame.data[5]  * 100.) / 160.0 );
			db->updateValue(schemaKeyForValueKey(FUEL_LEVEL), to_string(level), when);
		}
			break;

		case 0x244: //Door Status
		{
			int doors = 	 frame.data[0] ;
			db->updateValue(schemaKeyForValueKey(DOORS), to_string(doors), when);
			
			int locks = 	 frame.data[4] ;
			if(locks & 0x80)
				db->updateValue(schemaKeyForValueKey(DOORS_LOCK), to_string(false), when);
			else if(locks & 0x08)
				db->updateValue(schemaKeyForValueKey(DOORS_LOCK),  to_string(true), when);
			
		}
			break;

		case 0x2CE: // RPM
		{
			uint16_t xx = (frame.data[0] <<8 | frame.data[1]);
			if (xx != 0xFFFF){
				xx *= 4;
				db->updateValue(schemaKeyForValueKey(RPM), to_string(xx), when);
			};
		}
			break;
			
		case 0x3E6: //Clock Time Display
		{
			char str[10];
			sprintf (str, "%d:%02d:%02d", frame.data[0], frame.data[1],frame.data[2]);
			db->updateValue(schemaKeyForValueKey(CLOCK), string(str), when);
		}
			break;

		case 0x219: // JK VIN number
		{
			// this repeats with first byte as sequence number
			// --  once we get it stop updating

			static int stage = 0;
			
			if(_VIN.empty()) stage = 0;
			
			if (stage == 3) break;
			uint8_t b0 = frame.data[0];
			
			switch (stage) {
				case 0:
					if(b0 == 0){
						_VIN.append((char *)&frame.data[1], 7);
						stage++;
				}
					break;
					
				case 1:
					if(b0 == 1){
						_VIN.append((char *)&frame.data[1], 7);
						stage++;
				}
					break;
					
				case 2:
					if(b0 == 2){
						_VIN.append((char *)&frame.data[1], 7);
						db->updateValue(schemaKeyForValueKey(VIN), _VIN, when);
						stage++;
				}
					break;

			}
		}
			break;
			
		default:
		  break;
	};
	
}


string Wranger2010::descriptionForFrame(can_frame_t frame){
	string name = "";
	if(knownPid.count(frame.can_id)) {
		name = knownPid[frame.can_id];
	}

	return name;
}
 
