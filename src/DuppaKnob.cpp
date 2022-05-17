//
//  DuppaKnob.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/17/22.
//

#include "DuppaKnob.hpp"

#include "ErrorMgr.hpp"



DuppaKnob::DuppaKnob(){
	
	_isSetup = false;
	_twistCount = 0;
 }

DuppaKnob::~DuppaKnob(){
	stop();
}


bool DuppaKnob::begin(int deviceAddress){
	int error = 0;

	return begin(deviceAddress, error);
}

 
bool DuppaKnob::begin(int deviceAddress, int &error){
	bool status = false;

	uint8_t config = DuppaEncoder::INT_DATA
	| DuppaEncoder::WRAP_DISABLE
	| DuppaEncoder::DIRE_LEFT
	| DuppaEncoder::IPUP_ENABLE
	| DuppaEncoder::RMOD_X1
	| DuppaEncoder::RGB_ENCODER;
	

	status = _duppa.begin(deviceAddress, config,  error);
	
	if(status){
		_twistCount = 0;
		_isSetup = true;;
	}
	return status;
}

void DuppaKnob::stop(){
	_duppa.stop();
	_isSetup = false;
	
 }
 

bool  DuppaKnob::isConnected() {
	return _isSetup;
}



bool DuppaKnob::updateStatus(){
	uint8_t status;
	return updateStatus(status);
}

 
bool DuppaKnob::updateStatus(uint8_t &statusOut) {
	return _isSetup && _duppa.updateStatus(statusOut);
}

	
	bool DuppaKnob::wasClicked(){
	
 	return _isSetup && _duppa.wasClicked();
	
}


bool DuppaKnob::wasMoved( bool &cw){
	return _isSetup && _duppa.wasMoved(cw);
}


bool DuppaKnob::setColor(uint8_t red, uint8_t green, uint8_t blue ){
	return _isSetup && _duppa.setColor(red,green,blue);

}
