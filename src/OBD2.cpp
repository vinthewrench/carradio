//
//  OBD2.cpp
//  canhacker
//
//  Created by Vincent Moscaritolo on 3/25/22.
//

#include "OBD2.hpp"
#include "CANBusMgr.hpp"
#include "Utils.hpp"

#include <string.h>

#define CAN_OBD_MASK 0x00000700U /* standard frame format (SFF) */

typedef struct {
	string_view  						title;
	string_view  						description;
	FrameDB::valueSchemaUnits_t 	units;
} valueSchema_t;


static map<uint16_t, valueSchema_t> _J2190schemaMap ={
	{0x1940, 	{"OBD_TRANS_TEMP",	"Transmission Temperature",	FrameDB::DEGREES_C}},
	{0x132A, 	{"OBD_FUEL_??",	"FUEL?",					FrameDB::STRING}},
	{0x115C, 	{"OBD_OIL_PRESSURE",	"Oil Pressure?",	FrameDB::KPA}},
	{0x199A, 	{"OBD_TRANS_GEAR",	"Current Gear",			FrameDB::STRING}},
 };

static map<uint32_t, valueSchema_t> _service9schemaMap ={
	{ 0x02,	{"OBD_VIN",	"Vehicle Identification Number",	FrameDB::STRING}},

	{ 0x0A,  {"OBD_ECU_NAME", "ECU name",	FrameDB::STRING}},
		{  0x7e90A,  {"OBD_ECU_1_NAME", "ECU 1 name",	FrameDB::STRING}},
		{  0x7eA0A,  {"OBD_ECU_2_NAME", "ECU 2 name",	FrameDB::STRING}},
		{  0x7eB0A,  {"OBD_ECU_3_NAME", "ECU 3 name",	FrameDB::STRING}},
		{  0x7eC0A,  {"OBD_ECU_4_NAME", "ECU 4 name",	FrameDB::STRING}},
		{  0x7eD0A,  {"OBD_ECU_5_NAME", "ECU 5 name",	FrameDB::STRING}},
		{  0x7eE0A,  {"OBD_ECU_6_NAME", "ECU 6 name",	FrameDB::STRING}},
		{  0x7eF0A,  {"OBD_ECU_7_NAME", "ECU 7 name",	FrameDB::STRING}},
	
	{ 0x04,  {"OBD_CAL_ID", "Calibration ID",							FrameDB::DATA}},
	{ 0x06,  {"OBD_CVN", "Calibration Verification Numbers ",	FrameDB::DATA}},

};


static map<uint8_t, valueSchema_t> _otherServiceSchemaMap = {
	{ 3 , { "OBD_DTC_STORED", "Stored Diagnostic Trouble Codes", FrameDB::DTC}},
	{ 7 , { "OBD_DTC_PENDING", "Pending Diagnostic Trouble Codes", FrameDB::DTC}},
};

static map<uint32_t,  valueSchema_t> _schemaMap =
{
	{ 0x00,		{"OBD_PIDS_A",	"Supported PIDs [01-20]",	FrameDB::DATA}},
	{ 0x7e900,	{"OBD_PIDS_A1",	"ECU 1 Supported PIDs [01-20]",	FrameDB::DATA}},
	{ 0x7eA00,	{"OBD_PIDS_A2",	"ECU 2 Supported PIDs [01-20]",	FrameDB::DATA}},
	{ 0x7eB00,	{"OBD_PIDS_A3",	"ECU 3 Supported PIDs [01-20]",	FrameDB::DATA}},
	{ 0x7eC00,	{"OBD_PIDS_A4",	"ECU 4 Supported PIDs [01-20]",	FrameDB::DATA}},
	{ 0x7eD00,	{"OBD_PIDS_A5",	"ECU 5 Supported PIDs [01-20]",	FrameDB::DATA}},
	{ 0x7eE00,	{"OBD_PIDS_A6",	"ECU 6 Supported PIDs [01-20]",	FrameDB::DATA}},
	{ 0x7eF00,	{"OBD_PIDS_A7",	"ECU 7 Supported PIDs [01-20]",	FrameDB::DATA}},
	
	{ 0x01,	{"OBD_STATUS",	"Status since DTCs cleared",	FrameDB::SPECIAL}},
	{ 0x02,	{"OBD_FREEZE_DTC",	"DTC that triggered the freeze frame",	FrameDB::SPECIAL}},
	{ 0x03,	{"OBD_FUEL_STATUS",	"Fuel System Status",	FrameDB::STRING}},
	{ 0x04,	{"OBD_ENGINE_LOAD",	"Calculated Engine Load",	FrameDB::PERCENT}},
	{ 0x05,	{"OBD_COOLANT_TEMP",	"Engine Coolant Temperature",	FrameDB::DEGREES_C}},
	{ 0x06,	{"OBD_SHORT_FUEL_TRIM_1",	"Short Term Fuel Trim - Bank 1",	FrameDB::FUEL_TRIM}},
	{ 0x07,	{"OBD_LONG_FUEL_TRIM_1",	"Long Term Fuel Trim - Bank 1",	FrameDB::FUEL_TRIM}},
	{ 0x08,	{"OBD_SHORT_FUEL_TRIM_2",	"Short Term Fuel Trim - Bank 2",	FrameDB::FUEL_TRIM}},
	{ 0x09,	{"OBD_LONG_FUEL_TRIM_2",	"Long Term Fuel Trim - Bank 2",	FrameDB::FUEL_TRIM}},
	{ 0x0A,	{"OBD_FUEL_PRESSURE",	"Fuel Pressure",	FrameDB::KPA}},
	{ 0x0B,	{"OBD_INTAKE_PRESSURE",	"Intake Manifold Pressure",	FrameDB::KPA}},
	{ 0x0C,	{"OBD_RPM",	"Engine RPM",	FrameDB::RPM}},
	{ 0x0D,	{"OBD_VEHICLE_SPEED",	"Vehicle Speed",	FrameDB::KPH}},
	{ 0x0E,	{"OBD_TIMING_ADVANCE",	"Timing Advance",	FrameDB::DEGREES}},
	{ 0x0F,	{"OBD_INTAKE_TEMP",	"Intake Air Temp",	FrameDB::DEGREES_C}},
	{ 0x10,	{"OBD_MAF",	"Air Flow Rate (MAF)",	FrameDB::GPS}},
	{ 0x11,	{"OBD_THROTTLE_POS",	"Throttle Position",	FrameDB::PERCENT}},
	{ 0x12,	{"OBD_AIR_STATUS",	"Secondary Air Status",	FrameDB::STRING}},
	{ 0x13,	{"OBD_O2_SENSORS",	"O2 Sensors Present",	FrameDB::BINARY}},
	{ 0x14,	{"OBD_O2_B1S1",	"O2: Bank 1 - Sensor 1 Voltage",	FrameDB::VOLTS}},
	{ 0x15,	{"OBD_O2_B1S2",	"O2: Bank 1 - Sensor 2 Voltage",	FrameDB::VOLTS}},
	{ 0x16,	{"OBD_O2_B1S3",	"O2: Bank 1 - Sensor 3 Voltage",	FrameDB::VOLTS}},
	{ 0x17,	{"OBD_O2_B1S4",	"O2: Bank 1 - Sensor 4 Voltage",	FrameDB::VOLTS}},
	{ 0x18,	{"OBD_O2_B2S1",	"O2: Bank 2 - Sensor 1 Voltage",	FrameDB::VOLTS}},
	{ 0x19,	{"OBD_O2_B2S2",	"O2: Bank 2 - Sensor 2 Voltage",	FrameDB::VOLTS}},
	{ 0x1A,	{"OBD_O2_B2S3",	"O2: Bank 2 - Sensor 3 Voltage",	FrameDB::VOLTS}},
	{ 0x1B,	{"OBD_O2_B2S4",	"O2: Bank 2 - Sensor 4 Voltage",	FrameDB::VOLTS}},
	{ 0x1C,	{"OBD_OBD_COMPLIANCE",	"OBD Standards Compliance",	FrameDB::STRING}},
	{ 0x1D,	{"OBD_O2_SENSORS_ALT",	"O2 Sensors Present (alternate)",FrameDB::SPECIAL}},
	{ 0x1E,	{"OBD_AUX_INPUT_STATUS",	"Auxiliary input status (power take off)",	FrameDB::BOOL}},
	{ 0x1F,	{"OBD_RUN_TIME",	"Engine Run Time",	FrameDB::SECONDS}},
	
	{ 0x20,		{"OBD_PIDS_B",	"Supported PIDs [21-40]",	FrameDB::DATA}},
	{ 0x7e920,	{"OBD_PIDS_B1",	"ECU 1 Supported PIDs [21-40]",	FrameDB::DATA}},
	{ 0x7eA20,	{"OBD_PIDS_B2",	"ECU 2 Supported PIDs [21-40]",	FrameDB::DATA}},
	{ 0x7eB20,	{"OBD_PIDS_B3",	"ECU 3 Supported PIDs [21-40]",	FrameDB::DATA}},
	{ 0x7eC20,	{"OBD_PIDS_B4",	"ECU 4 Supported PIDs [21-40]",	FrameDB::DATA}},
	{ 0x7eD20,	{"OBD_PIDS_B5",	"ECU 5 Supported PIDs [21-40]",	FrameDB::DATA}},
	{ 0x7eE20,	{"OBD_PIDS_B6",	"ECU 6 Supported PIDs [21-40]",	FrameDB::DATA}},
	{ 0x7eF20,	{"OBD_PIDS_B7",	"ECU 7 Supported PIDs [21-40]",	FrameDB::DATA}},

	
	{ 0x21,	{"OBD_DISTANCE_W_MIL",	"Distance Traveled with MIL on",	FrameDB::KM}},
	{ 0x22,	{"OBD_FUEL_RAIL_PRESSURE_VAC",	"Fuel Rail Pressure (relative to vacuum)",	FrameDB::KPA}},
	{ 0x23,	{"OBD_FUEL_RAIL_PRESSURE_DIRECT",	"Fuel Rail Pressure (direct inject)",	FrameDB::KPA}},
	{ 0x24,	{"OBD_O2_S1_WR_VOLTAGE",	"02 Sensor 1 WR Lambda Voltage",	FrameDB::VOLTS}},
	{ 0x25,	{"OBD_O2_S2_WR_VOLTAGE",	"02 Sensor 2 WR Lambda Voltage",	FrameDB::VOLTS}},
	{ 0x26,	{"OBD_O2_S3_WR_VOLTAGE",	"02 Sensor 3 WR Lambda Voltage",	FrameDB::VOLTS}},
	{ 0x27,	{"OBD_O2_S4_WR_VOLTAGE",	"02 Sensor 4 WR Lambda Voltage",	FrameDB::VOLTS}},
	{ 0x28,	{"OBD_O2_S5_WR_VOLTAGE",	"02 Sensor 5 WR Lambda Voltage",	FrameDB::VOLTS}},
	{ 0x29,	{"OBD_O2_S6_WR_VOLTAGE",	"02 Sensor 6 WR Lambda Voltage",	FrameDB::VOLTS}},
	{ 0x2A,	{"OBD_O2_S7_WR_VOLTAGE",	"02 Sensor 7 WR Lambda Voltage",	FrameDB::VOLTS}},
	{ 0x2B,	{"OBD_O2_S8_WR_VOLTAGE",	"02 Sensor 8 WR Lambda Voltage",	FrameDB::VOLTS}},
	{ 0x2C,	{"OBD_COMMANDED_EGR",	"Commanded EGR",	FrameDB::PERCENT}},
	{ 0x2D,	{"OBD_EGR_ERROR",	"EGR Error",	FrameDB::PERCENT}},
	{ 0x2E,	{"OBD_EVAPORATIVE_PURGE",	"Commanded Evaporative Purge",	FrameDB::PERCENT}},
	{ 0x2F,	{"OBD_FUEL_LEVEL",	"Fuel Level",	FrameDB::PERCENT}},
	{ 0x30,	{"OBD_WARMUPS_SINCE_DTC_CLEAR",	"Number of warm-ups since codes cleared",	FrameDB::INT}},
	{ 0x31,	{"OBD_DISTANCE_SINCE_DTC_CLEAR",	"Distance traveled since codes cleared",	FrameDB::KM}},
	{ 0x32,	{"OBD_EVAP_VAPOR_PRESSURE",	"Evaporative system vapor pressure",	FrameDB::PA}},
	{ 0x33,	{"OBD_BAROMETRIC_PRESSURE",	"Barometric Pressure",	FrameDB::KPA}},
	{ 0x34,	{"OBD_O2_S1_WR_CURRENT",	"02 Sensor 1 WR Lambda Current",	FrameDB::MILLIAMPS}},
	{ 0x35,	{"OBD_O2_S2_WR_CURRENT",	"02 Sensor 2 WR Lambda Current",	FrameDB::MILLIAMPS}},
	{ 0x36,	{"OBD_O2_S3_WR_CURRENT",	"02 Sensor 3 WR Lambda Current",	FrameDB::MILLIAMPS}},
	{ 0x37,	{"OBD_O2_S4_WR_CURRENT",	"02 Sensor 4 WR Lambda Current",	FrameDB::MILLIAMPS}},
	{ 0x38,	{"OBD_O2_S5_WR_CURRENT",	"02 Sensor 5 WR Lambda Current",	FrameDB::MILLIAMPS}},
	{ 0x39,	{"OBD_O2_S6_WR_CURRENT",	"02 Sensor 6 WR Lambda Current",	FrameDB::MILLIAMPS}},
	{ 0x3A,	{"OBD_O2_S7_WR_CURRENT",	"02 Sensor 7 WR Lambda Current",	FrameDB::MILLIAMPS}},
	{ 0x3B,	{"OBD_O2_S8_WR_CURRENT",	"02 Sensor 8 WR Lambda Current",	FrameDB::MILLIAMPS}},
	{ 0x3C,	{"OBD_CATALYST_TEMP_B1S1",	"Catalyst Temperature: Bank 1 - Sensor 1",	FrameDB::DEGREES_C}},
	{ 0x3D,	{"OBD_CATALYST_TEMP_B2S1",	"Catalyst Temperature: Bank 2 - Sensor 1",	FrameDB::DEGREES_C}},
	{ 0x3E,	{"OBD_CATALYST_TEMP_B1S2",	"Catalyst Temperature: Bank 1 - Sensor 2",	FrameDB::DEGREES_C}},
	{ 0x3F,	{"OBD_CATALYST_TEMP_B2S2",	"Catalyst Temperature: Bank 2 - Sensor 2",	FrameDB::DEGREES_C}},
	
	{ 0x40,	{"OBD_PIDS_C",	"Supported PIDs [41-60]",	FrameDB::DATA}},
	{ 0x7e940,	{"OBD_PIDS_C1",	"ECU 1 Supported PIDs [41-60]",	FrameDB::DATA}},
	{ 0x7eA40,	{"OBD_PIDS_C2",	"ECU 2 Supported PIDs [41-60]",	FrameDB::DATA}},
	{ 0x7eB40,	{"OBD_PIDS_C3",	"ECU 3 Supported PIDs [41-60]",	FrameDB::DATA}},
	{ 0x7eC40,	{"OBD_PIDS_C4",	"ECU 4 Supported PIDs [41-60]",	FrameDB::DATA}},
	{ 0x7eD40,	{"OBD_PIDS_C5",	"ECU 5 Supported PIDs [41-60]",	FrameDB::DATA}},
	{ 0x7eE40,	{"OBD_PIDS_C6",	"ECU 6 Supported PIDs [41-60]",	FrameDB::DATA}},
	{ 0x7eF40,	{"OBD_PIDS_C7",	"ECU 7 Supported PIDs [41-60]",	FrameDB::DATA}},

	
	{ 0x41,	{"OBD_STATUS_DRIVE_CYCLE",	"Monitor status this drive cycle",	FrameDB::SPECIAL}},
	{ 0x42,	{"OBD_CONTROL_MODULE_VOLTAGE",	"Control module voltage",	FrameDB::VOLTS}},
	{ 0x43,	{"OBD_ABSOLUTE_LOAD",	"Absolute load value",	FrameDB::PERCENT}},
	{ 0x44,	{"OBD_COMMANDED_EQUIV_RATIO",	"Commanded equivalence ratio",	FrameDB::RATIO}},
	{ 0x45,	{"OBD_RELATIVE_THROTTLE_POS",	"Relative throttle position",	FrameDB::PERCENT}},
	{ 0x46,	{"OBD_AMBIANT_AIR_TEMP",	"Ambient air temperature",	FrameDB::DEGREES_C}},
	{ 0x47,	{"OBD_THROTTLE_POS_B",	"Absolute throttle position B",	FrameDB::PERCENT}},
	{ 0x48,	{"OBD_THROTTLE_POS_C",	"Absolute throttle position C",	FrameDB::PERCENT}},
	{ 0x49,	{"OBD_ACCELERATOR_POS_D",	"Accelerator pedal position D",	FrameDB::PERCENT}},
	{ 0x4A,	{"OBD_ACCELERATOR_POS_E",	"Accelerator pedal position E",	FrameDB::PERCENT}},
	{ 0x4B,	{"OBD_ACCELERATOR_POS_F",	"Accelerator pedal position F",	FrameDB::PERCENT}},
	{ 0x4C,	{"OBD_THROTTLE_ACTUATOR",	"Commanded throttle actuator",	FrameDB::PERCENT}},
	{ 0x4D,	{"OBD_RUN_TIME_MIL",	"Time run with MIL on",	FrameDB::MINUTES}},
	{ 0x4E,	{"OBD_TIME_SINCE_DTC_CLEARED",	"Time since trouble codes cleared",	FrameDB::MINUTES}},
	{ 0x4F,	{"OBD_unsupported",	"unsupported", FrameDB::UNKNOWN	}},
	{ 0x50,	{"OBD_MAX_MAF",	"Maximum value for mass air flow sensor",	FrameDB::GPS}},
	{ 0x51,	{"OBD_FUEL_TYPE",	"Fuel Type",	FrameDB::STRING}},
	{ 0x52,	{"OBD_ETHANOL_PERCENT",	"Ethanol Fuel Percent",	FrameDB::PERCENT}},
	{ 0x53,	{"OBD_EVAP_VAPOR_PRESSURE_ABS",	"Absolute Evap system Vapor Pressure",	FrameDB::KPA}},
	{ 0x54,	{"OBD_EVAP_VAPOR_PRESSURE_ALT",	"Evap system vapor pressure",	FrameDB::PA}},
	{ 0x55,	{"OBD_SHORT_O2_TRIM_B1",	"Short term secondary O2 trim - Bank 1",	FrameDB::PERCENT}},
	{ 0x56,	{"OBD_LONG_O2_TRIM_B1",	"Long term secondary O2 trim - Bank 1",	FrameDB::PERCENT}},
	{ 0x57,	{"OBD_SHORT_O2_TRIM_B2",	"Short term secondary O2 trim - Bank 2",	FrameDB::PERCENT}},
	{ 0x58,	{"OBD_LONG_O2_TRIM_B2",	"Long term secondary O2 trim - Bank 2",	FrameDB::PERCENT}},
	{ 0x59,	{"OBD_FUEL_RAIL_PRESSURE_ABS",	"Fuel rail pressure (absolute)",	FrameDB::KPA}},
	{ 0x5A,	{"OBD_RELATIVE_ACCEL_POS",	"Relative accelerator pedal position",	FrameDB::PERCENT}},
	{ 0x5B,	{"OBD_HYBRID_BATTERY_REMAINING",	"Hybrid battery pack remaining life",	FrameDB::PERCENT}},
	{ 0x5C,	{"OBD_OIL_TEMP",	"Engine oil temperature",	FrameDB::DEGREES_C}},
	{ 0x5D,	{"OBD_FUEL_INJECT_TIMING",	"Fuel injection timing",	FrameDB::DEGREES}},
	{ 0x5E,	{"OBD_FUEL_RATE",	"Engine fuel rate",	FrameDB::LPH}},
	{ 0x5F,	{"OBD_unsupported",	"unsupported",	FrameDB::UNKNOWN}}
};



 
//inline  std::string hexDumpOBDData(canid_t can_id, uint8_t mode, uint8_t pid, valueSchema_t* schema,
//											  uint16_t len, uint8_t* data) {
//
//	char      	 lineBuf[256] ;
//	char       	 *p = lineBuf;
//	bool showHex = true;
//	bool showascii = false;
//	uint16_t ext = 0;
//
//	if(mode == 0x22){
//		ext = (pid << 8) | data[0];
//		data++;
//		len--;
//	}
//
//	p += sprintf(p, "%03X %02X, %02X [%d] ",  can_id, mode, pid, len);
//
//	if(mode == 9 && pid == 2){
//		showHex = false;
//		showascii = true;
//	}
//
//	if(showHex){
//		for (int i = 0; i < len; i++) p += sprintf(p,"%02X ",data[i]);
//		for (int i = 7; i >  len ; i--) p += sprintf(p,"   ");
//	}
//
//	if(showascii){
//		for (int i = 0; i < len; i++)  {
//		uint8_t c = data[i] & 0xFF;
//		if (c > ' ' && c < '~')
//			*p++ = c ;
//		else {
//			*p++ = '.';
//		}
//	}
//	}
//	p += sprintf(p,"  ");
//
//	if(schema){
//		p += sprintf(p, "\t%s",  string(schema->description).c_str());
//	}
//
//	*p++ = 0;
//
//	return string(lineBuf);
//}

OBD2::OBD2(){
	_ecu_messages.clear();
}

void OBD2::registerSchema(CANBusMgr* canBus){
	
	_canBus = canBus;
	FrameDB* frameDB = _canBus->frameDB();
	
	for (auto it = _schemaMap.begin(); it != _schemaMap.end(); it++){
		valueSchema_t*  schema = &it->second;
		
		vector<uint8_t> obd_request = {};
		
		if(it->first < 0xff)
			obd_request = {0x01, static_cast<uint8_t>(it->first) };
		
		frameDB->addSchema(schema->title,  {schema->title, schema->description, schema->units}, obd_request);
	}
	
	for (auto it = _service9schemaMap.begin(); it != _service9schemaMap.end(); it++){
		valueSchema_t*  schema = &it->second;
		frameDB->addSchema(schema->title,  {schema->title, schema->description, schema->units});
	}
	
	for (auto it = _J2190schemaMap.begin(); it != _J2190schemaMap.end(); it++){
		valueSchema_t*  schema = &it->second;
		
		uint16_t b = it->first;
		vector<uint8_t> obd_request =  {0x22, static_cast<uint8_t>(b >> 8) , static_cast<uint8_t>(b & 0xff)};
		frameDB->addSchema(schema->title,  {schema->title, schema->description, schema->units}, obd_request);
	}
	
	for (auto it = _otherServiceSchemaMap.begin(); it != _otherServiceSchemaMap.end(); it++){
		valueSchema_t*  schema = &it->second;
		frameDB->addSchema(schema->title,  {schema->title, schema->description, schema->units});
	}
}

void OBD2::reset() {
	_ecu_messages.clear();
}


void OBD2:: processFrame(FrameDB* db,string ifName, can_frame_t frame, time_t when){

	canid_t can_id = frame.can_id & CAN_SFF_MASK;
	
	//ISO 15765-2
	// is it an OBD2 request
	if((can_id & CAN_OBD_MASK) != 0x700) return;
 
	uint8_t frame_type = frame.data[0]>> 4;
	
	switch( frame_type){
		case 0: // single frame
		{
			// is it a OBD response? Only record responses
			if((frame.data[1] & 0x40) == 0x40){
				uint8_t len = frame.data[0] & 0x07;
				uint8_t mode = frame.data[1] & 0x3f;
				uint8_t pid = frame.data[2];
				
				processOBDResponse(db, when, can_id,
										 	mode, pid, len-2, &frame.data[3]);
			}
		}
			break;
			
		case 1: // first CAN message of a fragmented OBD-2 message
		{
			// if its one of ours we need to ask for more here..
			// send a flow control Continue To Send (CTS) frame
	 
			if( _canBus->sendFrame(ifName, can_id - 8 , {0x30, 0x00, 0x0A}, NULL)){
				
				// only store te continue if we were successful.
				obd_state_t s;
		 
				s.rollingcnt = 1;
				s.total_len = ((frame.data[0] & 0x0f) | frame.data[1]) - 2;
				s.mode = frame.data[2] & 0x1F;
				s.pid = frame.data[3];
				memcpy(s.buffer, &frame.data[4], 4);
				s.current_len = 4;
				_ecu_messages[can_id] = s;
  			}
		}
			break;
		
		case 2: // consecutive message
		{
			auto it = _ecu_messages.find(can_id);
			if(it != _ecu_messages.end()){
				auto s = &it->second;
				uint8_t rollingcnt =  (frame.data[0] & 0x0f);
				if(s->rollingcnt != rollingcnt) break;
				s->rollingcnt = (s->rollingcnt+1) & 0x0f;  // MODULO 16
				
				uint16_t len = s->total_len - s->current_len;
				if(len > 7) len = 7;
				memcpy(s->buffer + s->current_len, &frame.data[1], len);
				s->current_len += len;
				
				if(s->current_len == s->total_len) {
					processOBDResponse(db, when,
											 can_id, s->mode, s->pid,
											  s->total_len, s->buffer);
					_ecu_messages.erase(it);
				}
			}
			break;
		}
		case 3:  // ignore flow control Continue To Send (CTS) frame
			break;
		default: ;
			// not handled?
	
	}
 };

// value calculation and corrections
static string valueForData(canid_t can_id, uint8_t mode, uint8_t pid,
									valueSchema_t* schema,
									uint16_t len, uint8_t* data){
	string value = string();

	
	if(mode == 0x22){	 // mode 22  J2190
		
		uint16_t ext = (pid << 8) | data[0];
		if(ext == 0x115C) {// oil pressure
			value = to_string(data[0] * 2 );
			}
	}
	else if(mode == 1 || mode == 2){
		switch(pid){
			case 0x42: //OBD_CONTROL_MODULE_VOLTAGE
				value = to_string( ((data[0] <<8 )| data[1]) / 1000.00) ;
				break;
			
			case 0x6:
			case 0x7:
			case 0x8:
			case 0x9:  //FUEL_TRIM
				value = to_string(  (data[0] * (100.0/128.0)) - 100. ) ;
				break;
			
			case 0x04:	// Calculated engine load
			case 0x11:	// Throttle position
			case 0x2C:	//Commanded EGR
			case 0x2E:  // Commanded evaporative purge
			case 0x2F:  //Fuel Tank Level Input
			case 0x45:	// Relative throttle position
			case 0x47:	// Absolute throttle position B
			case 0x48:	// Absolute throttle position C
			case 0x49:	// Accelerator pedal position D
			case 0x4A:	// Accelerator pedal position E
			case 0x4B:	// Accelerator pedal position F
			case 0x4C://  Commanded throttle actuator
			case 0x52:// 	Ethanol fuel %
			case 0x5A:// 	Relative accelerator pedal position
			case 0x5B:// 	 Hybrid battery pack remaining life
				value = to_string(data[0] * (100.0/255.0)) ;
				break;
	
			case 0x3C:	// Catalyst Temperature: Bank 1, Sensor 1
			case 0x3D:	// Catalyst Temperature: Bank 2, Sensor 1
			case 0x3E:	// Catalyst Temperature: Bank 1, Sensor 2
			case 0x3F:	// Catalyst Temperature: Bank 2, Sensor 2
			case 0x7C:	// Diesel Particulate filter (DPF) temperature
				value = to_string( (((data[0] <<8 )| data[1]) / 10.00) -40) ;
			break;

			case 0x14:	// Oxygen Sensor 1 Voltage
			case 0x15:	// Oxygen Sensor 2 Voltage
			case 0x16:	// Oxygen Sensor 3 Voltage
			case 0x17:	// Oxygen Sensor 4 Voltage
			case 0x18:	// Oxygen Sensor 5 Voltage
			case 0x19:	// Oxygen Sensor 6 Voltage
			case 0x1A:	// Oxygen Sensor 7 Voltage
			case 0x1B:	// Oxygen Sensor 8 Voltage
				value = to_string(data[0] /200.) ;
				break;

				
				
			case 0x34 ://Oxygen Sensor 1 Air-Fuel Equivalence Ratio
			case 0x35 ://Oxygen Sensor 2 Air-Fuel Equivalence Ratio
			case 0x36 ://Oxygen Sensor 3 Air-Fuel Equivalence Ratio
			case 0x37 ://Oxygen Sensor 4 Air-Fuel Equivalence Ratio
			case 0x38 ://Oxygen Sensor 5 Air-Fuel Equivalence Ratio
			case 0x39 ://Oxygen Sensor 6 Air-Fuel Equivalence Ratio
			case 0x3A ://Oxygen Sensor 7 Air-Fuel Equivalence Ratio
			case 0x3B ://Oxygen Sensor 8 Air-Fuel Equivalence Ratio
			case 0x44 ://Commanded Air-Fuel Equivalence Ratio
				
				value = to_string( (((data[0] <<8 )| data[1]) <<1 ) /  65536.) ;
				break;
				
			default: break;
		}
	}
	else  if(mode == 9) {
		switch(pid){
			case 0x02: //Vehicle Identification Number (VIN)
			{
							// skip Number of data items: (NODI)
				data++; len --;
				size_t len1 = strnlen((char* )data, len );
				value = string( (char* )data, len1);
			}
 				break;
		
			case 0x0A: // ECU's/module's acronym and text name
			{
				// skip Number of data items: (NODI)
				data++; len --;
				size_t len1 = strnlen((char* )data, len );
				value = string( (char* )data, len1); // grab ECU acronym,
 				if(len1 < 4 && len > 4 ) {
					// there might be a filler byte so skip the first 4 chars and append the rest
					len = len - 4;
					data+= 4;
					len1 = strnlen((char* )data, len );
					value.append( string( (char* )data, len1));
				}
 			}
				break;
			default: break;
		}
	}
	
	// convert to hex string
	if(schema->units	 == FrameDB::DATA){
		value = hexStr(data, len);
	}
	else if(schema->units	 == FrameDB::DTC){
		static char codechar[4] = {'P', 'C', 'B', 'U'};
		
	for( int i = 0; i < len; i +=2){
			string DTC;
			if( len - 2 < 0 ) break;	// error check for short packets
			DTC = string(1, codechar[data[i] >> 6]) ;	 // the upper 2 bits of the first byte
			DTC+=	to_string((data[i] >> 6) & 0b0011);
			DTC+=	to_string(data[i] & 0xf);
			DTC+=	to_string(data[i+1] >> 4 );
			DTC+=	to_string(data[i+1] & 0xf);
			value += DTC + " ";
		}
	}

	
	if(value.empty()){
		switch(len){
				case 1: value = to_string(data[0]); break;
				case 2: value = to_string((data[0] <<8 )| data[1]); break;
				case 3: value = to_string( (data[0] <<12)|(data[1] <<8) | data[2]); break;

#warning FIX THIS  warning: converting the result of '<<' to a boolean; did you mean '(data[0] << 16) != 0'?
			
			case 4: value = to_string( (data[0] <<16) || (data[1] <<12)|(data[2] <<8) | data[3]); break;

			default:  value =string( (char* )data, len); break;
			}
		}
	
	return value;;
}


void OBD2::processOBDResponse(FrameDB* db,time_t when,
										canid_t can_id,
									   uint8_t mode, uint8_t pid, uint16_t len, uint8_t* data){
	
	valueSchema_t* schema = NULL;
 
	switch(mode){
		case 1:
		case 2:
		{
			// any other ECU than 7e8 uses the alternate Schema ID
			uint32_t altPid = pid;
			if(can_id != 0x7e8) {
				switch(pid) {
					case 0x00:
					case 0x20:
					case 0x40:
						altPid =  (can_id <<8) | altPid;
						break;
				}
			}
			
			if(_schemaMap.count(altPid)){
				schema = &_schemaMap[altPid];
			}
		}
			break;
			
		case 4: // clear diag codes?
			break;
			
		case 3: // Show stored Diagnostic Trouble Codes
			schema =  &_otherServiceSchemaMap[3];
		break;
			
		case 7: // Show pending Diagnostic Trouble Codes
			schema =  &_otherServiceSchemaMap[7];
		break;
			
		case 9:
		{
			// any other ECU than 7e8 uses the alternate Schema ID
			uint32_t altPid = pid;
			if(can_id != 0x7e8) {
				switch(pid) {
					case 0x0A:
						altPid =  (can_id <<8) | altPid;
						break;
				}
			}
		 
			if(_service9schemaMap.count(altPid)){
				schema = &_service9schemaMap[altPid];
			}
		}
			break;
			
		case 0x22:
		{
			uint16_t ext = (pid << 8) | data[0];
			if(_J2190schemaMap.count(ext)){
				schema = &_J2190schemaMap[ext];
			}
			len--;
			data++;
		}
	}
	
 
	if(!schema){
//		printf("No schema for mode: %d  pid %d\n", mode, pid);
		return;
	}
	
	string value = valueForData(can_id, mode,pid, schema, len, data);
	db->updateValue(schema->title  ,value,when);
}

 
string OBD2::descriptionForFrame(can_frame_t frame){
	string name = "";
	
	canid_t can_id = frame.can_id & CAN_SFF_MASK;
	
	// is it an OBD2 request
	if((can_id & CAN_OBD_MASK) == 0x700) {
		
		if(can_id == 0x7DF)
			name = string("OBD ALL");
		else if(can_id >= 0x7e0 && can_id <= 0x7E7)
			name = string("OBD REQ: ") + to_string(can_id - 0x7e0) ;
		else 	if(can_id >= 0x7e8 && can_id <= 0x7EF){
			name = string("OBD RES: ") + to_string(can_id - 0x7e8);
		}
	}

	
 	return name;
}

 


/*
 7E2 (01 0F) [1] 99                     INTAKE_TEMP
 7E8 (01 46) [1] 32                     AMBIANT_AIR_TEMP
 7E8 (01 05) [1] 73                     COOLANT_TEMP
 7E8 (01 0C) [2] 09 56                  RPM
 7E8 (01 0D) [1] 22                     SPEED
 7E8 (01 07) [1] 6B                     LONG_FUEL_TRIM_1
 7E8 (01 09) [1] 6F                     LONG_FUEL_TRIM_2
 7E8 (01 04) [1] 44                     ENGINE_LOAD
 7E8 (01 10) [2] 02 7C                  MAF
 7E8 (01 42) [2] 30 A3                  CONTROL_MODULE_VOLTAGE
 7E8 (01 1F) [2] 00 47                  RUN_TIME
 7EA (22 19) [1] 3D                     SAE J2190 Code 22 (1940) TRANS_TEMP
 7EA (22 13) [2] 06 61                  SAE J2190 Code 22 (132A) FUEL
 7E8 (22 11) [1] 5A                     SAE J2190 Code 22 (115C) OIL_PRESSURE
 7EA (22 19) [1] 01                     SAE J2190 Code 22 (199A) GEAR
 OK>


 */
