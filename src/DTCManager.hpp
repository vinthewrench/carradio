//
//  DTCManager.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 11/9/22.
//

#pragma once


#include "CommonDefs.hpp"
#include "PiCarCAN.hpp"

using namespace std;


class DTCManager  {
	
public:
	
	DTCManager ();
	~DTCManager ();
	
	bool begin();
 	void stop();
	bool isConnected() ;

	
private:
 
	static void processWanglerRadioRequestsWrapper(void* context,
															  string ifName, canid_t can_id, vector<uint8_t> bytes,
															  unsigned long timeStamp);

	void processWanglerRadioRequests(string ifName, canid_t can_id, vector<uint8_t> bytes,
								 	 unsigned long timeStamp);

	void	processPrivateODB(time_t when,  canid_t can_id, uint8_t service_id, bool isRequest, 
									vector<uint8_t> bytes);

	void	processWanglerRadioPID21(uint8_t pid);
	void	processWanglerRadioPID1A(uint8_t pid);
	void	processWanglerRadioPID18(uint8_t pid,  vector<uint8_t> bytes);

//	void	processISOTPFlowControlFrame(time_t when,  canid_t can_id, uint8_t* data);
 
	bool	sendISOTPReply(canid_t can_id, uint8_t service_id, uint8_t pid,  vector<uint8_t> bytes,  int* error = NULL );
	bool	sendISOTPReply(canid_t can_id, uint8_t service_id,  vector<uint8_t> bytes,  int* error = NULL );

	bool					_isSetup;
  };
