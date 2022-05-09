//
//  QTKnob.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/9/22.
//

#include "QTKnob.hpp"
#include "ErrorMgr.hpp"



QTKnob::QTKnob(){
	
	_isSetup = false;
	_twistCount = 0;
 }

QTKnob::~QTKnob(){
	stop();
}


bool QTKnob::begin(int deviceAddress){
	int error = 0;

	return begin(deviceAddress, error);
}

 
bool QTKnob::begin(int deviceAddress, int &error){
	bool status = false;

	status = _twist.begin(deviceAddress, error);
	
	if(status){
		_twistCount = 0;
		_isSetup = true;;
 	}
	return status;
}

void QTKnob::stop(){
	_twist.stop();
	_isSetup = false;
	
 }
 

bool  QTKnob::isConnected() {
	return _isSetup;
}


 
bool QTKnob::wasClicked(){
	
	bool clicked = false;
	
	if(_isSetup
		&& _twist.isClicked(clicked)
		&& clicked)
		return true;

	return false;
	
}


bool QTKnob::wasMoved( bool &up){
 
	bool moved = false;
	
	if(_isSetup
		&& _twist.isMoved(moved)
		&& moved) {
		
		if(_twist.getDiff(_twistCount, true)) {
			up = _twistCount > 0;
			return true;
		}
	}
	
	return false;
}
