//
//  PiCarCAN.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/25/22.
//

#include "PiCarCAN.hpp"


//#define DEBUG_CAN 1

static map<PiCarCAN::pican_bus_t, string>bus_map  = {};


PiCarCAN::PiCarCAN() {
	_isSetup = false;
 
	
#if DEBUG_CAN
	 
	bus_map[CAN_GM] = "vcan0";
	bus_map[CAN_JEEP] = "vcan0";

	//	_CANbus.registerProtocol("vcan0", &_jeep);
	//	_CANbus.registerProtocol("vcan0", &_obdii);
	
	_CANbus.registerProtocol(bus_map[CAN_GM], &_gmlan);
	_CANbus.registerProtocol(bus_map[CAN_GM], &_obdii);
	_CANbus.registerHandler(bus_map[CAN_GM]);

#else
	
	bus_map[CAN_GM] = "can1";
	bus_map[CAN_JEEP] = "can0";

	_CANbus.registerProtocol(bus_map[CAN_GM], &_gmlan);
	_CANbus.registerProtocol(bus_map[CAN_GM], &_obdii);
	_CANbus.registerHandler(bus_map[CAN_GM]);

	// jk bus does not do obdii
	_CANbus.registerProtocol(bus_map[CAN_JEEP], &_jeep);
	_CANbus.registerHandler(bus_map[CAN_JEEP]);

#endif
	
}

PiCarCAN::~PiCarCAN(){
	stop();
}



bool PiCarCAN::begin(){
	int error = 0;
	
	return begin (error);
}


bool PiCarCAN::begin( int &error){
 	_isSetup = false;
 
	
#if defined(__APPLE__)
	_isSetup = true;
	
	_CANbus.start(bus_map[CAN_GM], error) ;
	
#else
	 
#if DEBUG_CAN
	_isSetup = (_CANbus.start(bus_map[CAN_GM], error) );

#else
	_isSetup = (_CANbus.start(bus_map[CAN_GM], error)
				&& _CANbus.start(bus_map[CAN_JEEP], error) );

#endif
	
#endif
 
	return _isSetup;
}
 
void PiCarCAN::stop(){
	
	if(_isSetup) {
	 		_isSetup = false;
		}
}


bool PiCarCAN:: isConnected(){
	return _isSetup;
}


bool PiCarCAN::reset(){
 
	return true;
}
 

bool PiCarCAN::lastFrameTime(pican_bus_t bus, time_t &time){
	
	string ifName  = bus == CAN_ALL?"":bus_map[bus];
	return _CANbus.lastFrameTime(ifName, time);
}


bool PiCarCAN::totalPacketCount(pican_bus_t bus, size_t &count){
	string ifName  = bus == CAN_ALL?"":bus_map[bus];
	return _CANbus.totalPacketCount(ifName, count);
}

bool PiCarCAN::packetsPerSecond(pican_bus_t bus, size_t &count){
	string ifName  = bus == CAN_ALL?"":bus_map[bus];
	return _CANbus.packetsPerSecond(ifName, count);
}


bool PiCarCAN::resetPacketCount(pican_bus_t bus){
	string ifName  = bus == CAN_ALL?"":bus_map[bus];
	return _CANbus.resetPacketCount(ifName);
}

bool PiCarCAN::getStatus(vector<CANBusMgr::can_status_t> & stats){
	return  _CANbus.getStatus(stats);
}


bool PiCarCAN::request_OBDpolling(string key){
	return _CANbus.request_OBDpolling(key);
}

bool PiCarCAN::cancel_OBDpolling(string key){
	return _CANbus.cancel_OBDpolling(key);
}

bool PiCarCAN::descriptionForDTCCode(string code, string& description){
	return _dtc.descriptionForDTCCode(code,description);
}


bool PiCarCAN::sendDTCEraseRequest(){
	return _CANbus.sendDTCEraseRequest();
}

bool PiCarCAN::setPeriodicCallback (pican_bus_t bus, int64_t delay,
												CANBusMgr::periodicCallBackID_t & callBackID,
												void* context,
												CANBusMgr::periodicCallBack_t cb ){
	string ifName  = bus == CAN_ALL?"":bus_map[bus];
 	return _CANbus.setPeriodicCallback(ifName, delay, callBackID, context, cb);
 }

bool PiCarCAN::removePeriodicCallback (CANBusMgr::periodicCallBackID_t  callBackID){
	return _CANbus.removePeriodicCallback(callBackID);
}


// frame handler
bool PiCarCAN::registerISOTPHandler(pican_bus_t bus,
												canid_t can_id,
												CANBusMgr::ISOTPHandlerCB_t  cb,  void* context){
	string ifName  = bus == CAN_ALL?"":bus_map[bus];
	return _CANbus.registerISOTPHandler(ifName, can_id, cb, context);
}



void PiCarCAN::unRegisterISOTPHandler(pican_bus_t bus, canid_t can_id, CANBusMgr::ISOTPHandlerCB_t cb ){
	
	string ifName  = bus == CAN_ALL?"":bus_map[bus];
	return _CANbus.unRegisterISOTPHandler(ifName, can_id, cb);
}


bool PiCarCAN::sendFrame(pican_bus_t bus, canid_t can_id, vector<uint8_t> bytes,  int* error ){
	string ifName  = bus == CAN_ALL?"":bus_map[bus];
	return _CANbus.sendFrame(ifName, can_id, bytes, error);
}

bool PiCarCAN::sendISOTP(pican_bus_t bus, canid_t can_id, canid_t reply_id,   vector<uint8_t> bytes,  int* error ){
	string ifName  = bus == CAN_ALL?"":bus_map[bus];
	return _CANbus.sendISOTP(ifName, can_id, reply_id, bytes, error);
}

