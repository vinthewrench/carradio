//
//  GMLAN.hpp
//  canhacker
//
//  Created by Vincent Moscaritolo on 3/23/22.
//

#pragma once

#include "CanProtocol.hpp"

#include <map>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <string>

class GMLAN: public CanProtocol {
	
public:
  	GMLAN();
	
	virtual void registerSchema(CANBusMgr*);
	virtual void reset();

	virtual void processFrame(FrameDB* db,string ifName, can_frame_t frame, time_t when);
	virtual string descriptionForFrame(can_frame_t frame);
 
		
private:

	string_view schemaKeyForValueKey(int valueKey);
	 
	// specific updates
	void processPlatGenStatus(FrameDB* db, can_frame_t frame, time_t when);

	void processEngineGenStatus(FrameDB* db, can_frame_t frame, time_t when);

	void processEngineGenStatus1(FrameDB* db, can_frame_t frame, time_t when);

	void processEngineGenStatus2(FrameDB* db, can_frame_t frame, time_t when);

	void processEngineGenStatus3(FrameDB* db, can_frame_t frame, time_t when);

	void processEngineGenStatus5(FrameDB* db, can_frame_t frame, time_t when);

	void processEngineTorqueStatus3(FrameDB* db, can_frame_t frame, time_t when);
	
	void processFuelSystemRequest2(FrameDB* db, can_frame_t frame, time_t when);

	void processEngineGenStatus4(FrameDB* db, can_frame_t frame, time_t when);

	void processTransmissionStatus3(FrameDB* db, can_frame_t frame, time_t when);

	void processTransmissionStatus2(FrameDB* db, can_frame_t frame, time_t when);

	void processPlatformConfiguration(FrameDB* db, can_frame_t frame, time_t when);

	void processTransOutRotation(FrameDB* db, can_frame_t frame, time_t when);

	void processVehicleSpeed(FrameDB* db, can_frame_t frame, time_t when);
	 
};


