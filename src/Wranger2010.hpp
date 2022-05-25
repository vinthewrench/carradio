//
//  Wranger2010.hpp
//  canhacker
//
//  Created by Vincent Moscaritolo on 3/23/22.
//
 

#include "CanProtocol.hpp"
 
class Wranger2010: public CanProtocol {
	
public:
 
	Wranger2010();

	virtual void registerSchema(CANBusMgr*);
	virtual void reset();
	virtual void processFrame(FrameDB* db,string ifName, can_frame_t frame, time_t when);

	virtual string descriptionForFrame(can_frame_t frame);
 
 
private:
	string_view schemaKeyForValueKey(int valueKey);

	string _VIN;
};


