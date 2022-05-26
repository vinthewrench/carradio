//
//  PiCarCAN.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/25/22.
//

#include "PiCarCAN.hpp"

#define DEBUG_CAN 1

PiCarCAN::PiCarCAN() {
	_isSetup = false;
 
	
#if DEBUG_CAN
	 
	//	_CANbus.registerProtocol("vcan0", &_jeep);
	//	_CANbus.registerProtocol("vcan0", &_obdii);
	
	_CANbus.registerProtocol("vcan0", &_gmlan);
	_CANbus.registerProtocol("vcan0", &_obdii);
	_CANbus.registerHandler("vcan0");

#else
	_CANbus.registerProtocol("can1", &_gmlan);
	_CANbus.registerProtocol("can1", &_obdii);
	_CANbus.registerHandler("can1");

	_CANbus.registerProtocol("can0", &_jeep);
	_CANbus.registerProtocol("can0", &_obdii);
	_CANbus.registerHandler("can0");

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
#else
	 
#if DEBUG_CAN
	_isSetup = (_CANbus.start("vcan0", error) );

#else
	_isSetup = (_CANbus.start("can0", error)
				&& _CANbus.start("can1", error)
					);

#endif
	
#endif
 
	return _isSetup;
}



void PiCarCAN::stop(){
	
	if(_isSetup) {
	 		_isSetup = false;
		}
}



bool PiCarCAN::reset(){
 
	return true;
}
 

bool PiCarCAN::lastFrameTime(time_t &time){
	return _CANbus.lastFrameTime("", time);
}


bool PiCarCAN::packetCount(size_t &count){
	return _CANbus.packetCount("", count);
}


bool PiCarCAN::resetPacketCount(){
	return _CANbus.resetPacketCount("");
}
