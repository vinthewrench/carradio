//
//  OBD2.hpp
//  canhacker
//
//  Created by Vincent Moscaritolo on 3/25/22.
//
 
#include "CanProtocol.hpp"
#pragma once
 
 
class OBD2: public CanProtocol {
	
public:
	
	OBD2();
	
	
	virtual void registerSchema(CANBusMgr*);

	virtual void reset();
	virtual void processFrame(FrameDB* db,string ifName, can_frame_t frame, time_t when);

	virtual string descriptionForFrame(can_frame_t frame);
  
private:
	
	void processOBDResponse(FrameDB* db,time_t when,
									canid_t can_id,
									uint8_t mode, uint8_t pid, uint16_t len, uint8_t* data);
			 
	typedef struct {
		uint8_t			mode;
		canid_t			can_id;
		uint8_t			pid;
		uint8_t			rollingcnt; 	// next expected cnt
		uint16_t			total_len;
		uint16_t			current_len;

		uint8_t			buffer[4096];
		} obd_state_t;

	map<canid_t,obd_state_t> _ecu_messages;
	
	CANBusMgr*		_canBus;  // needs a backpointer
};


