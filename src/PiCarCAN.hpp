//
//  PiCarCAN.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/25/22.
//

#pragma once


#include "CommonDefs.hpp"

#include <time.h>

#include "CANBusMgr.hpp"

#include "GMLAN.hpp"
#include "OBD2.hpp"
#include "Wranger2010.hpp"
#include "DTCcodes.hpp"

using namespace std;


class PiCarCAN {
	
public:

	typedef enum  {
		CAN_ALL= 0,
		CAN_GM,
		CAN_JEEP,
	}pican_bus_t;

	PiCarCAN();
	~PiCarCAN();
		
	bool begin();
	bool begin(int &error);
	bool isConnected();
	void stop();
	bool reset();

	bool lastFrameTime(pican_bus_t bus, time_t &time);
	bool totalPacketCount(pican_bus_t bus, size_t &count);
	bool packetsPerSecond(pican_bus_t bus, size_t &count);
	bool resetPacketCount(pican_bus_t bus);
 
	bool getStatus(vector<CANBusMgr::can_status_t> & stats);
 
	FrameDB* frameDB() {return  _CANbus.frameDB();};

	// frame handler
	bool registerISOTPHandler(pican_bus_t bus, canid_t can_id,  CANBusMgr::ISOTPHandlerCB_t  cb = NULL, void* context = NULL);
	void unRegisterISOTPHandler(pican_bus_t bus, canid_t can_id, CANBusMgr::ISOTPHandlerCB_t cb );

	// OBD request need to be polled.. this starts and stops the polling
	bool request_OBDpolling(string key);
	bool cancel_OBDpolling(string key);

	bool descriptionForDTCCode(string code, string& description);
	bool sendDTCEraseRequest();

	bool setPeriodicCallback (pican_bus_t bus, int64_t delay,
									  CANBusMgr::periodicCallBackID_t & callBackID,
									  void* context,
									  CANBusMgr::periodicCallBack_t cb);
	
	bool removePeriodicCallback (CANBusMgr::periodicCallBackID_t  callBackID);

	bool sendFrame(pican_bus_t bus, canid_t can_id, vector<uint8_t> bytes,  int *error = NULL);
   bool sendISOTP(pican_bus_t bus, canid_t can_id, canid_t reply_id,   vector<uint8_t> bytes,  int* error = NULL );
	
private:
	bool 				_isSetup = false;

	CANBusMgr		_CANbus;
	Wranger2010		_jeep;
	GMLAN 			_gmlan;
	OBD2				_obdii;
	DTCcodes			_dtc;
};

