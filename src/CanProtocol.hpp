//
//  CanProtocol.hpp
//  canhacker
//
//  Created by Vincent Moscaritolo on 3/23/22.
//

#pragma once

#include "CommonDefs.hpp"
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <mutex>
#include <bitset>
#include <cstdint>

#if defined(__APPLE__)
// used for cross compile on osx
#include "can.h"
typedef struct can_frame can_frame_t;

#else
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#endif

typedef struct can_frame can_frame_t;

using namespace std;

class FrameDB;
class CANBusMgr;

class CanProtocol {

public:
 
	virtual void registerSchema(CANBusMgr*) {};
	
	virtual void reset()  {};
	virtual void processFrame(FrameDB* db, string ifName,  can_frame_t frame, time_t when){};
	virtual string descriptionForFrame(can_frame_t frame)  {return "";};
	
	virtual bool canBePolled() {return false;};

};

