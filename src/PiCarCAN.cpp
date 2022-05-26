//
//  PiCarCAN.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/25/22.
//

#include "PiCarCAN.hpp"


PiCarCAN::PiCarCAN() {
	_isSetup = false;
 
	_CANbus.registerProtocol("can1", &_gmlan);
	_CANbus.registerProtocol("can1", &_obdii);
	_CANbus.registerHandler("can1");

//	_CANbus.registerProtocol("can0", &_jeep);
//	_CANbus.registerProtocol("can0", &_obdii);
//
//	_CANbus.registerHandler("can0");
	
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
	 
	_isSetup = (_CANbus.start("can0", error)
//					&& _CANbus.start("can1", error)
					);
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
 
