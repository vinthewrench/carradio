//
//  DTCManager.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 11/9/22.
//

#include "DTCManager.hpp"
#include "PiCarMgr.hpp"
#include "PiCarCAN.hpp"
 


DTCManager::DTCManager(){
	_isSetup = false;

}

DTCManager::~DTCManager(){
	_isSetup = false;

 }


bool DTCManager::begin( ){
 
	bool status = false;
 	_isSetup = false;
 
	// register Wangler Radio Frame Handler
	PiCarCAN*	can 	= PiCarMgr::shared()->can();
	status = can->registerFrameHandler( PiCarCAN::CAN_JEEP, 0x6B0, processWanglerRadioFrameWrapper, this);
 
	_isSetup = status;
	return true;
 }

 
void DTCManager::stop(){
	
	if(_isSetup  ){
		PiCarCAN*	can 	= PiCarMgr::shared()->can();
		can->unRegisterFrameHandler(PiCarCAN::CAN_JEEP, 0x6B0, processWanglerRadioFrameWrapper);
	}
	
	_isSetup = false;
	
}

bool  DTCManager::isConnected() {
  bool val = false;
	
	val = _isSetup;

  return val;
};


// MARK: -   Wangler Radio Frame Handler



void DTCManager::processWanglerRadioFrameWrapper(void* context,
														 string ifName, canid_t can_id,
														 can_frame_t frame, unsigned long timeStamp){
	DTCManager* d = (DTCManager*)context;
	
	d->processWanglerRadioFrame(ifName, can_id, frame, timeStamp);
}


void DTCManager::processWanglerRadioFrame(string ifName, canid_t can_id, can_frame_t frame, unsigned long timeStamp){
	
	uint8_t frame_type = frame.data[0]>> 4;

	switch( frame_type){
		case 0: // single frame
		{
			uint8_t len = frame.data[0] & 0x07;
			bool REQ = (frame.data[1] & 0x40)  == 0 ;
			
			uint8_t service_id = REQ?frame.data[1]: frame.data[1] & 0x3f;

			// only handle requests
			if(REQ){
				processPrivateODB(timeStamp, can_id, service_id,  len -1 , &frame.data[2]);
 			}
	 
		}
			break;
		case 3:  //  flow control Continue To Send (CTS) frame
					// handler if we have more..
			break;
			
		default: ;
			// not handled?
			
	}

}

void	DTCManager::processPrivateODB(time_t when,  canid_t can_id, uint8_t service_id,
												uint16_t len, uint8_t* data){
	
	
	if(len > 0){
		uint8_t pid = data[0];
		len--;
		data++;
		
		printf("(%02x,%02X)  ", service_id, pid);
		
		if(len > 0){
			printf("%2d: ", len);
			for(int i = 0; i < len; i++)
				printf("%02x ", data[i]);
			
			if(len > 8){
				printf("\n\t\t|");
				for(int i = 0; i < len; i++){
					uint8_t c =  data[i];
					if (c > ' ' && c < '~')
						printf("%c", data[i]);
					else {
						printf(".");
					}
				}
				printf("|");
				
			}
			
		}
		printf("\n");
		
	}
}
 
