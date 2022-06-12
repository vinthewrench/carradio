//
//  GMLAN.cpp
//  canhacker
//
//  Created by Vincent Moscaritolo on 3/23/22.
//

#include "GMLAN.hpp"
#include "CANBusMgr.hpp"
#include "FrameDB.hpp"

#include <bitset>

#define PLAT_GEN_STAT	0x1F1

#define TRAN_ROT		  0x0C7
#define PLAT_CONF 		  0x4E9
#define TRANS_STAT_3 	   0x4C9
#define TRANS_STAT_2 	   0x1F5

#define ENGINE_GEN_STAT   0x1A1

#define ENGINE_GEN_STAT_1  0x0C9
#define ENGINE_GEN_STAT_4 0x4C1
#define ENGINE_GEN_STAT_2 0x3D1
#define ENGINE_GEN_STAT_3 0x3F9

#define ENGINE_TORQUE_STAT_2 0x1C3

#define ENGINE_GEN_STAT_5 0x4D1
#define FUEL_SYSTEM_2  0x1EF
#define VEHICLE_SPEED_DIST 0x3E9
 


 // derived from GMW8762
static map<uint, string> knownPid = {
{ 0x0C1, "Driven Wheel Rotational Status"},
{ 0x0C5, "Non Driven Wheel Rotational Status"},
{ TRAN_ROT, "Transmission Output Rotational Status"},
{ ENGINE_GEN_STAT_1, "Engine General Status 1"},
{ 0x0F1, "Brake Apply Status"},
{ 0x0F9, "Transmission General Status 1"},
{ 0x150, "High Voltage Battery Information 1"},
{ 0x154, "High Voltage Battery Information 2"},
{ 0x158, "High Voltage Battery Information 3"},
{ 0x17D, "Antilock_Brake_and_TC_Status_HS"},
{ ENGINE_GEN_STAT, "Engine General Status"},
{ ENGINE_TORQUE_STAT_2, "Engine Torque Status 2"},
{ 0x1C4, "Torque Request Status"},
{ 0x1C5, "Driver Intended Axle Torque Status"},
{ 0x1C7, "Chassis Engine Torque Request 1"},
{ 0x1C8, "Launch Control Request"},
{ 0x1CC, "Secondary Axle Status"},
{ 0x1CE, "Secondary Axle Control"},
{ 0x1CF, "Secondary Axle General Information"},
{ 0x1D0, "Front Axle Status"},
{ 0x1D1, "Rear Axle Status"},
{ 0x1E1, "Cruise Control Switch Status"},
{ 0x1E5, "Steering Wheel Angle"},
{ 0x1E9, "Chassis General Status 1"},
{ 0x1EA, "Alternative Fuel System Status"},
{ 0x1EB, "Fuel System Status"},
{ 0x1ED, "Fuel System Request"},
{ FUEL_SYSTEM_2, "Fuel System Request 2"},
{ PLAT_GEN_STAT, "Platform General Status"},
{ 0x1F3, "Platform Transmission Requests"},
{ TRANS_STAT_2, "Transmission General Status 2"},
{ 0x1F9, "PTO Command Data"},
{ 0x2B0, "Starter_Generator_Status_3"},
{ 0x2C3, "Engine Torque Status 3"},
{ 0x2CB, "Adaptive Cruise Axle Torque Request"},
{ 0x2F9, "Chassis General Status 2"},
{ 0x3C1, "Powertrain Immobilizer Data"},
{ 0x3C9, "Platform Immobilizer Data"},
{ ENGINE_GEN_STAT_2, "Engine General Status 2"},
{ 0x3E1, "Engine_BAS_Status_1"},
{ VEHICLE_SPEED_DIST, "Vehicle Speed and Distance"},
{ 0x3ED, "Vehicle_Limit_Speed_Control_Cmd"},
{ 0x3F1, "Platform Engine Control Request"},
{ 0x3F9, "Engine General Status 3"},
{ 0x3FB, "Engine Fuel Status"},
{ 0x451, "Gateway LS General Information"},
{ ENGINE_GEN_STAT_4, "Engine General Status 4"},
{ TRANS_STAT_3, "Transmission General Status 3"},
{ ENGINE_GEN_STAT_5, "Engine General Status 5"},
{ 0x4D9, "Fuel System General Status"},
{ 0x4E1, "Vehicle Identification Digits 10 thru 17"},
{ PLAT_CONF, "Platform Configuration Data"},
{ 0x4F1, "Powertrain Configuration Data"},
{ 0x4F3, "Powertrain Configuration Data 2"},
{ 0x772, "Diagnostic Trouble Code Information 1 Extended"},
{ 0x77A, "Diagnostic Trouble Code Information 2 Extended"},
{ 0x77F, "Diagnostic Trouble Code Information 3 Extended"},
{ 0x3F3, "Power Pack General Status"},
{ 0x3F7, "Hybrid T emperature Status"},
{ 0x1D9, "Hybrid Balancing Request"},
{ 0x1DE, "Hybrid Battery General Status"}
};

typedef  enum  {
	ENGINE_RPM,
	ENGINE_RUNNING,
	FUEL_CONSUMPTION,
	THROTTLE_POS,
	FAN_SPEED,
	OLF,
	OLF_RESET,
	TEMP_COOLANT,
	TEMP_TRANSMISSION,
	PRESSURE_OIL,
	TEMP_OIL,
	VEHICLE_SPEED,
	MASS_AIR_FLOW,
	BAROMETRIC_PRESSURE,
	TEMP_AIR_INTAKE,
	TEMP_AIR_AMBIENT,
	TRANS_GEAR,
	ENGINE_TORQUE,
// warnimg lights
	GM_CHECK_ENGINE,
	GM_CHANGE_OIL,
	GM_REDUCED_POWER,
	GM_CHECK_FUELCAP,
	GM_OIL_LOW,
} value_keys_t;


typedef FrameDB::valueSchema_t valueSchema_t;

static map<value_keys_t,  valueSchema_t> _schemaMap = {
	{ENGINE_RPM,			{"GM_ENGINE_RPM",			"Engine RPM",							FrameDB::RPM}},
	{ENGINE_RUNNING,		{"GM_ENGINE_RUNNING",	"Engine Run Active",					FrameDB::BOOL}},
	{FUEL_CONSUMPTION,	{"GM_FUEL_CONSUMPTION",	"Instantaneous Fuel Consumption Rate", 	FrameDB::LPH}},
	{THROTTLE_POS,			{"GM_THROTTLE_POS",		"Throttle Pedal Position",				FrameDB::PERCENT}},
	{FAN_SPEED,				{"GM_FAN_SPEED",			"Fan Speed", 								FrameDB::PERCENT}},
	{TEMP_COOLANT,			{"GM_COOLANT_TEMP",		"Engine Coolant Temperature", 		FrameDB::DEGREES_C}},
	{TEMP_TRANSMISSION,	{"GM_TRANS_TEMP",			"Transmission Temperature",			FrameDB::DEGREES_C}},
	{PRESSURE_OIL,			{"GM_OIL_PRESSURE",		"Oil Pressure",							FrameDB::KPA}},
	{VEHICLE_SPEED,		{"GM_VEHICLE_SPEED",		"Vehicle Speed",							FrameDB::KPH}},
	{MASS_AIR_FLOW,		{"GM_MAF",					"Air Flow Rate (MAF)",					FrameDB::GPS}},
	{BAROMETRIC_PRESSURE,{"GM_BAROMETRIC_PRESSURE",	"Barometric Pressure",				FrameDB::KPA}},
	{TEMP_AIR_INTAKE,		{"GM_INTAKE_TEMP",		"Intake Air Temp",						FrameDB::DEGREES_C}},
	{TEMP_AIR_AMBIENT,	{"GM_AMBIANT_AIR_TEMP",	"Ambient air temperature",				FrameDB::DEGREES_C}},
	{TRANS_GEAR,			{"GM_TRANS_GEAR",			"Current Gear",							FrameDB::STRING}},
	{ENGINE_TORQUE,		{"GM_ENGINE_TORQUE",		"Engine Torque Actual",					FrameDB::NM}},
	{TEMP_OIL,				{"GM_OIL_TEMP",			"Engine Oil Temperature",				FrameDB::DEGREES_C}},
	{OLF,						{"GM_OLF",					"Engine Oil Remaining Life",			FrameDB::PERCENT}},
	{OLF_RESET,				{"GM_OLF_RESET",			"RESET Oil Life Performed",			FrameDB::BOOL}},

	{GM_CHECK_ENGINE,		{"GM_CHECK_ENGINE",		"Engine Diagnostic Trouble Code Present Indication On",		FrameDB::BOOL}},
	{GM_CHANGE_OIL,		{"GM_CHANGE_OIL",			"Engine Oil Change Indication On",									FrameDB::BOOL}},
	{GM_REDUCED_POWER,	{"GM_REDUCED_POWER",		"Reduced Power Indication On",										FrameDB::BOOL}},
	{GM_CHECK_FUELCAP,	{"GM_CHECK_FUELCAP",		"Check Fuel Filler Cap Indication On",								FrameDB::BOOL}},
	{GM_OIL_LOW,			{"GM_OIL_LOW",					"Engine Oil Level Low Indication On",							FrameDB::BOOL}},
};


GMLAN::GMLAN(){
	reset();
}


void GMLAN::reset(){
	
}

 

void GMLAN::registerSchema(CANBusMgr* cbMgr){
	
	FrameDB* frameDB = cbMgr->frameDB();
	
	for (auto it = _schemaMap.begin(); it != _schemaMap.end(); it++){
		valueSchema_t*  schema = &it->second;
		frameDB->addSchema(schema->title,  {schema->title, schema->description, schema->units});
	}

 }

string_view GMLAN::schemaKeyForValueKey(int valueKey) {
	 
	auto key = static_cast<value_keys_t>( valueKey);
	valueSchema_t*  schema = &_schemaMap[key];
	return schema->title;
  }
 

string GMLAN::descriptionForFrame(can_frame_t frame){
	string name = "";
	if(knownPid.count(frame.can_id)) {
		name = knownPid[frame.can_id];
	}

	return name;
}
 


void  GMLAN::processFrame(FrameDB* db,string ifName, can_frame_t frame, time_t when){
 
	switch(frame.can_id) {

		case PLAT_GEN_STAT:
			processPlatGenStatus(db, frame,when);
			break;
			
		case ENGINE_GEN_STAT:
			processEngineGenStatus(db, frame,when);
			break;
			
		case ENGINE_GEN_STAT_1:
			processEngineGenStatus1(db, frame,when);
			break;
		
		case ENGINE_GEN_STAT_2:
			processEngineGenStatus2(db, frame,when);
			break;

		case ENGINE_GEN_STAT_3:
			processEngineGenStatus3(db, frame,when);
			break;

		case ENGINE_GEN_STAT_5:
			processEngineGenStatus5(db, frame,when);
			break;
	
		case FUEL_SYSTEM_2:
			processFuelSystemRequest2(db, frame,when);
			break;
		
		case ENGINE_GEN_STAT_4:
			processEngineGenStatus4(db, frame,when);
			break;

		case TRANS_STAT_3:
			processTransmissionStatus3(db, frame,when);
			break;

		case TRANS_STAT_2:
			processTransmissionStatus2(db, frame,when);
			break;
 
		case ENGINE_TORQUE_STAT_2:
			processEngineTorqueStatus3(db, frame,when);
			break;
 			
		case PLAT_CONF:
			processPlatformConfiguration(db, frame,when);
			break;
		
		case TRAN_ROT:
			processTransOutRotation(db, frame,when);
			break;

		case VEHICLE_SPEED_DIST:
			processVehicleSpeed(db, frame,when);
			break;

			
		default:
		  break;
	};
	
}


void GMLAN::processEngineTorqueStatus3(FrameDB* db, can_frame_t frame, time_t when){
	
	bool torqueValid = (frame.data[0] & 0x10) == 0x10;
	
	if(torqueValid) {
		int N = 	(frame.data[0] & 0x0f) <<8 | frame.data[0];
		float torque =  (N * 0.50) - 848;
		db->updateValue(schemaKeyForValueKey(ENGINE_TORQUE), to_string(torque), when);
	}
}

void GMLAN::processPlatGenStatus(FrameDB* db, can_frame_t frame, time_t when){

};

void GMLAN::processEngineGenStatus(FrameDB* db, can_frame_t frame, time_t when){

};

void GMLAN::processEngineGenStatus1(FrameDB* db, can_frame_t frame, time_t when){

	
	bool running =  frame.data[0] & 0x80;
	db->updateValue(schemaKeyForValueKey(ENGINE_RUNNING), to_string(running), when);

	int rpm = 	frame.data[1] <<8 | frame.data[2];
	db->updateValue(schemaKeyForValueKey(ENGINE_RPM), to_string(rpm), when);
 
 };

void GMLAN::processEngineGenStatus2(FrameDB* db, can_frame_t frame, time_t when){
	float tPos = (frame.data[1] * 100)/255.0;
	db->updateValue(schemaKeyForValueKey(THROTTLE_POS), to_string(tPos),when);

	float ifc =  ((frame.data[4] & 3)  <<8 | frame.data[5]) * 0.025 ;
	db->updateValue(schemaKeyForValueKey(FUEL_CONSUMPTION), to_string(ifc),when);
	
	bool olf_reset =  frame.data[4] & 0x10;
	db->updateValue(schemaKeyForValueKey(OLF_RESET), to_string(olf_reset),when);
};

void GMLAN::processEngineGenStatus3(FrameDB* db, can_frame_t frame, time_t when){

	float fan = (frame.data[5]* 100) / 255.0;
	db->updateValue(schemaKeyForValueKey(FAN_SPEED), to_string(fan),when);

	float oilLife = (frame.data[6]* 100) / 255.0;
	db->updateValue(schemaKeyForValueKey(OLF), to_string(oilLife),when);

	
};

void GMLAN::processEngineGenStatus5(FrameDB* db, can_frame_t frame, time_t when){
	
	
	bitset<8> byte0 = frame.data[0];
	bitset<8> byte3 = frame.data[3];
	bitset<8> byte6 = frame.data[6];
 
	
	
// Note: I have suspct about the validity bit
//	if(byte0.test(6)) //Engine Oil Pressure Validity
	{
		float oilpress =  (frame.data[2] * 4);
		db->updateValue(schemaKeyForValueKey(PRESSURE_OIL), to_string(oilpress),when);
	}
	
 	if(byte0.test(7)) //Engine Oil Temperature Validity
	{
		float oiltemp =  (frame.data[1] - 40);
		db->updateValue(schemaKeyForValueKey(TEMP_OIL), to_string(oiltemp),when);
	}
	
	bool oilLow =  byte0.test(4);
	db->updateValue(schemaKeyForValueKey(GM_OIL_LOW), to_string(oilLow),when);

	bool changeOil =  byte0.test(3);
	db->updateValue(schemaKeyForValueKey(GM_CHANGE_OIL), to_string(changeOil),when);

	bool reducedPower = byte3.test(7);
	db->updateValue(schemaKeyForValueKey(GM_REDUCED_POWER), to_string(reducedPower),when);

	bool checkFuelCap = byte3.test(5);
	db->updateValue(schemaKeyForValueKey(GM_CHECK_FUELCAP), to_string(checkFuelCap),when);

	bool checkEngine = byte6.test(2);
	db->updateValue(schemaKeyForValueKey(GM_CHECK_ENGINE), to_string(checkEngine),when);
};



void GMLAN::processFuelSystemRequest2(FrameDB* db, can_frame_t frame, time_t when){
	bool mafValid =  frame.data[0] & 0x80;

	if(mafValid){
		float maf =  ((frame.data[2])  <<8 | frame.data[3]) * 0.01;
		db->updateValue(schemaKeyForValueKey(MASS_AIR_FLOW), to_string(maf),when);

	}


};

void GMLAN::processEngineGenStatus4(FrameDB* db, can_frame_t frame, time_t when){

	
	float baro		= 	(frame.data[1]  / 2.0);
	db->updateValue(schemaKeyForValueKey(BAROMETRIC_PRESSURE), to_string(baro),when);
 
	float coolTemp = 	frame.data[2];
	db->updateValue(schemaKeyForValueKey(TEMP_COOLANT), to_string(coolTemp),when);

	float airIn 	=	frame.data[3];
	db->updateValue(schemaKeyForValueKey(TEMP_AIR_INTAKE), to_string(airIn),when);

	float airAmb =  	(frame.data[4] *.5);
	db->updateValue(schemaKeyForValueKey(TEMP_AIR_AMBIENT), to_string(airAmb),when);

};

void GMLAN::processTransmissionStatus2(FrameDB* db, can_frame_t frame, time_t when){

	bool gearValid =  !(frame.data[0] & 0x10);

	if(gearValid){
		uint8_t gear = frame.data[0] & 0x0F;
		
		string gearCode[] = {
			"NotSupported",
			"1",
			"2",
			"3",
			"4",
			"5",
			"6",
			"7",
			"8",
			"??",
			"??",
			"xx",
			"CVTForward",
			"N",
			"R",
			"P"
		};
		
		db->updateValue(schemaKeyForValueKey(TRANS_GEAR), gearCode[gear] ,when);

	}
};

void GMLAN::processTransmissionStatus3(FrameDB* db, can_frame_t frame, time_t when){
	float transTemp =  frame.data[1];
	db->updateValue(schemaKeyForValueKey(TEMP_TRANSMISSION), to_string(transTemp),when);

};



void GMLAN::processPlatformConfiguration(FrameDB* db, can_frame_t frame, time_t when){

};

void GMLAN::processTransOutRotation(FrameDB* db, can_frame_t frame, time_t when){

};

void GMLAN::processVehicleSpeed(FrameDB* db, can_frame_t frame, time_t when){
	
	bool speedValid = (frame.data[0] & 0x80) == 0x00;
//	bool distValid = (frame.data[2] & 0x40) == 0x00;
	speedValid = true;
//
	if(speedValid) {
		float speed	= (((frame.data[0] & 0x7F) <<8)  | frame.data[1]) * 0.015625;
		db->updateValue(schemaKeyForValueKey(VEHICLE_SPEED), to_string(speed),when);
	}
	
//	if(distValid) {
//		float dist	= (((frame.data[2] & 0x1F) <<8)  | frame.data[3]) / 8;
//		db->updateValue(_value_key_map[dist], to_string(speed),when);
//
//	}
};


// MARK: -  Useful CAN messages

/*
can messages

#normal
cansend can1 4D1#000002031E3E0000

# change oil  GM_CHANGE_OIL
cansend can1 4D1#080002031E3E0000

#check engine GM_CHECK_ENGINE
cansend can1 4D1#000002031E3E0400

#check fuelcap GM_CHECK_FUELCAP
cansend can1 4D1#000002271E3E8000

#reduced power GM_REDUCED_POWER
cansend can1 4D1#000002271E3E8000

#low oil GM_OIL_LOW
cansend can1 4D1#100002031E3E0000


#oil reset  (40 TIMES 4 SECONDS
cansend can1 3D1#0033800010000000

cansend can1 3D1#0033800000000000

#OIL 99.22 %   GM_OLF
cansend can1 3F9#800000005500FD0E

#OIL 100%
cansend can1 3F9#040000005500FF0D

#OIL 65.88 %
cansend can1 3F9#040000005500A80D
*/
