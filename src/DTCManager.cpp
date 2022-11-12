//
//  DTCManager.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 11/9/22.
//

#include "DTCManager.hpp"
#include "PiCarMgr.hpp"
#include "PiCarCAN.hpp"
#include "RadioMgr.hpp"
#include "AudioOutput.hpp"

constexpr canid_t WRANGLER_RADIO_REQ = 0x6B0;
constexpr canid_t WRANGLER_RADIO_REPLY = 0x516;

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
	status = can->registerISOTPHandler( PiCarCAN::CAN_JEEP, WRANGLER_RADIO_REQ, processWanglerRadioRequestsWrapper, this);
 
	_isSetup = status;
	return true;
 }

 
void DTCManager::stop(){
	
	if(_isSetup  ){
		PiCarCAN*	can 	= PiCarMgr::shared()->can();
		can->unRegisterISOTPHandler(PiCarCAN::CAN_JEEP, WRANGLER_RADIO_REQ, processWanglerRadioRequestsWrapper);
	}
	
	_isSetup = false;
	
}

bool  DTCManager::isConnected() {
  bool val = false;
	
	val = _isSetup;

  return val;
};


// MARK: -   Wangler Radio Frame Handler



void DTCManager::processWanglerRadioRequestsWrapper(void* context,
														 string ifName, canid_t can_id,
															vector<uint8_t> bytes, unsigned long timeStamp){
	DTCManager* d = (DTCManager*)context;
	
	d->processWanglerRadioRequests(ifName, can_id, bytes, timeStamp);
}


void DTCManager::processWanglerRadioRequests(string ifName, canid_t can_id, vector<uint8_t> bytes, unsigned long timeStamp){
	
	uint len = (uint)bytes.size();
	
	if(len){
		bool isRequest = (bytes[0] & 0x40)  == 0 ;
		uint8_t service_id = bytes[0] & 0x3f;
 		bytes.erase(bytes.begin());
		processPrivateODB(timeStamp, can_id, service_id, isRequest, bytes);
	}
}
 
void	DTCManager::processPrivateODB(time_t when,  canid_t can_id, uint8_t service_id, bool isRequest, vector<uint8_t> bytes){
 
	// only process requests
	if(!isRequest) return;
	
	//		{
	//			printf("rcv  %03x %02x [%2d] ", can_id, service_id, (int) bytes.size() );
	//			for(int i = 0; i < bytes.size(); i++) printf("%02x ", bytes[i]);
	//			printf("|\n");
	//		}
	
	uint len = (uint)bytes.size();
	if( len > 0) {
		
		uint8_t pid = bytes[0];
		bytes.erase(bytes.begin());
		
		switch (service_id) {
				
			case 0x3E:		// heartbeat?
				if(pid == 0x01){
					// send reply as raw frame
					PiCarCAN*	can 	= PiCarMgr::shared()->can();
					can->sendFrame(PiCarCAN::CAN_JEEP,  WRANGLER_RADIO_REPLY, {0x01, 0x7E, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00}, NULL);
				}
				break;
				
			case 0x1A:
				processWanglerRadioPID1A(pid);
				break;
				
			case 0x21:
				processWanglerRadioPID21(pid);
				break;
				
			case 0x18:
				processWanglerRadioPID18(pid, bytes);
				break;
				
			default:
				break;
		}
		
	}
}

void	DTCManager::processWanglerRadioPID18(uint8_t pid,  vector<uint8_t> bytes){
	
 			/*
			 
			 DTC code is different than ODB DTC code
			 reply   [6]   94 86 60 94 81 60
			 Codes   b1486 | B1481  the 0x60 is an unknown?
			 
			 // no errors
			 can0  6B0   [8]  04 18 00 FF 00 00 00 00   '........'
			 can0  516   [8]  02 58 00 00 00 00 00 00   '.X......'
			 
			 */
			// send reply NO error
			
//		sendISOTPReply( WRANGLER_RADIO_REPLY, 0x18, {0x00});  // no error
	sendISOTPReply( WRANGLER_RADIO_REPLY, 0x18, {0x09,
		0x94,0x80,0x60,
		0x94,0x81,0x60,
		0x94,0x82,0x60 ,
		0x94,0x83,0x60,
		0x94,0x84,0x60,
		0x94,0x85,0x60 ,
		0x94,0x86,0x60,
		0x94,0x87,0x60,
		0x94,0x88,0x60,
 	});

}

void	DTCManager::processWanglerRadioPID1A(uint8_t pid){
	
	switch (pid) {
		case 0x87: // ECU part VAR
		
			/*
				can0  7F0   [8]  02 1A 87 00 00 00 00 00   '........'
				can0  53E   [8]  10 16 5A 87 02 84 02 05   '..Z.....'
				can0  7F0   [8]  30 00 00 00 00 00 00 00   '0.......'
				can0  53E   [8]  21 FF 00 03 08 03 11 35   '!......5'
				can0  53E   [8]  22 36 30 34 36 30 30 36   '"6046006'
				can0  53E   [8]  23 41 4C 00 00 00 00 00   '#AL.....'
			 */
			
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x1A,  pid,
									  {0x02, 0x84, 0x02,			// could be variant 02 AMP NTG4 [02,78,02]
										0x05,							// diag 05
										0xFF,							// supplier FF  Harmon Becker
										0x00, 0x03,					//HW:(00,03)
										0x08, 0x03, 0x11,			// SW:(08,03,11)
										0x35, 0x36, 0x30, 0x34, 0x36, 0x30, 0x30, 0x36, 0x41, 0x4C //Part:56046006AL
 									},  NULL);
 			break;
			
		case 0x88: // VIN Number  (original) (Var)
			/*
 			 */
			break;

		case 0x90: // VIN Number  (current) (Var)
			/*
 			 */
			break;

		default:
			break;

	}
}
			

void	DTCManager::processWanglerRadioPID21(uint8_t pid){
	
	PiCarMgr*	mgr 	= PiCarMgr::shared();
	RadioMgr*					radio 	= mgr->radio();
	
	switch (pid) {
			
		case 0x09:  //Antenna Detect ( 1 Bytes)
			//			19	  Audio detect true
			//			11    Audio detect false
			//			11    antenna present
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, {0x19});
			break;
			
		case 0x0E: // Signal strength  ( 1 byte)
		{
			uint8_t signal_strenth = 100; //  AM FM signal strength 	0-120
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, {signal_strenth});
		}
			break;
			
		case 0x10: // Mode (6 bytes)
			/*
			 [6] 0f 00 07 04 00 0f  ==  FM
			 [6] 0f 00 07 02 00 0f  ==  AM
			 [6] 0f 00 07 00 01 0f  == SAT
			 [6] 0f 00 07 00 02 0f  == AUX
			 */
		{
			RadioMgr::radio_mode_t  mode  = radio->radioMode();
			vector<uint8_t> data = {0x0f, 0x00, 0x07};
			
			switch(mode){
				case RadioMgr::BROADCAST_AM:
					data.push_back(0x02);
					data.push_back(0x00);
					break;
					
				case RadioMgr::BROADCAST_FM:
				case RadioMgr::VHF:
				case RadioMgr::GMRS:
					data.push_back(0x04);
					data.push_back(0x00);
					break;
					
				case RadioMgr::AUX:
				case RadioMgr::AIRPLAY:
					data.push_back(0x00);
					data.push_back(0x01);
					break;
					
				default:
					data.push_back(0x00);
					data.push_back(0x00);
					break;
			}
			data.push_back(0x0F);
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, data);
		}
			break;
			
		case 0x11:// EQUALIZER ? (6 bytes)
		{
			AudioOutput* audio 	= mgr->audio();
			// note that equalizer (not vol)  should be  1h(-9) - 0ah (0) - 13h (+9)
 			vector<uint8_t> data = {
				static_cast<uint8_t> ( audio->volume() * 38) ,
				static_cast<uint8_t> ( ( audio->bass() * 10) + 10),
				static_cast<uint8_t> ( ( audio->treble() * 10) + 10),
				static_cast<uint8_t> ( ( audio->balance() * 10) + 10),
				static_cast<uint8_t> ( ( audio->fader() * 10) + 10),
				static_cast<uint8_t> ( ( audio->midrange() * 10) + 10)
			};
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, data);
		}
			break;
			
		case 0x12: //  Frequency	(6 Bytes)
		{
			RadioMgr::radio_mode_t  mode  = radio->radioMode();
			//			00	00	yy	yy	xx xx
			//				yyyy = FM Freq (04 37= 107.9, 03 6d =87.7)
			//				xxxx =  AM Freq (005a0 = 1440)
			
			if(mode == RadioMgr::BROADCAST_FM){
				uint16_t  freq =  radio->frequency() /1.0e5;
				vector<uint8_t> data = { 0x00, 0x00,  0x00, 0x00,
					static_cast<uint8_t> (freq >> 8),
					static_cast<uint8_t> (freq & 0xFF) };
				sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, data);
			}
			else {
				sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, { 0x00, 0x00,  0x00, 0x00, 0x00, 0x00});
				
			}
		}
			break;
			
		case 0x16: // Model code	(10 bytes)
		{
			//  |....RES...|  ???
			vector<uint8_t> data = { 0x00, 0x00, 0x00, 0x00, 0x52, 0x45, 0x53, 0x20, 0x10, 0x00 };
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, data);
		}
			break;
			
		case 0x18: //Market (1 bytes)
			// USA market
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, {0x00});
			break;
			
		case 0x25:  // Sirius ID (var)
		{
			string SiriusID = "044056306622";
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, Utils::getByteVector(SiriusID));
		}
			break;
			
		case 0x30:  // key position (6 bytes)
			/*
			 [6] 41 01 00 48 0a 00 		= ACC/RUN  - radio on
			 [6] 41 01 00 48 0a 00 		= RUN radio on
			 [6] 41 01 00 48 2a 00		= RUN radio off
			 [6] 41 01 00 48 2a 00 		= ACC Radio off
			 [6] 41 01 00 48 2a 00 		= OFF radio off
			 [6] 40 01 00 48 20 00 		= OFF radio off
			 */
#warning  FINISH key position
			break;
			
		case 0x34: // ????  (5 bytes)
			/*
			 [5] 00 00 00 00 04
			 */
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, {0x00, 0x00, 0x00, 0x00, 0x04});
			break;
			
		case 0x35: // Language pref english??  (6 bytes)
			/*
			 [5]  01 01 FF 00 00
			 */
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, {0x01, 0x01, 0xFF, 0x00, 0x00});
			break;
			
		case 0x36: // ????  (6 bytes)
			/*
			 [5] 03 00 00 00 00
			 */
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, {0x03, 0x00, 0x00, 0x00, 0x00});
			break;
			
		case 0x44://  VIN REQUEST  (Var)   [ 7 bytes ] [ SEQ #, 0,1,2]
#warning  FINISH VIN REQUEST
			/*
			 44	12	31	39	41	4C	32	32	30	01	2D	39	A7	01	01	FF	FF	00	00	00	|19AL220.-9........|
			 44	12	31	4A	34	42	41	36	48	00	2D	39	A7	01	01	FF	FF	00	00	00	|1J4BA6H.-9........|
			 44	12	30	36	30	00	00	00	00	02	2D	39	A7	01	01	FF	FF	00	00	00	|060.....-9........|
			 */
			break;
			
		case 0x49:  // Rear camera false  (5 bytes)
			/*
			 [5] 00 FF 00 00 30
			 [5] 00 FF 00 00 00
			 [5] 00 FF 00 00 A7
 			 */
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, {0x00, 0xFF, 0x00, 0x00, 0x00});
 			break;
			
		case 0x50: // ????  (6 bytes)
			/*
			 [6] 50 06 00 10 00 0A 0A 00
			 [6]
			 */
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, {0x50, 0x06, 0x00, 0x00, 0x00, 0x0A, 0x0A, 0x00});
 			break;
			
		case 0x52: // ????  (13 bytes)
			/*
			 00 00 0A 07 01 0A 00 FF 00 00 00 00 02
			 */
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid,
				{0x00 ,0x00, 0x0A, 0x07, 0x01, 0x0A, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x02});
  			break;
			
		case 0xE1:   //   Radio Serial Number  (var)
		{
			/*
			 cansend can0 6B0#0221E10000000000;  sleep 0.1; cansend can0  6B0#3000000000000000
			 
			 6B0#0221E10000000000	'.!......'
			 516#101061E154313141	'..a.T11A'
			 6B0#3000000000000000	'0.......'
			 516#2148313735303930	'!H175090'
			 516#2232373531313137	'"2751117'
			 */
			string serialNumber = mgr->serialNumber();
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid, Utils::getByteVector(serialNumber));
		}
			break;
			
		case 0xEA:  //  ????  (5 bytes)
			/*
			 can0  6B0   [8]  02 21 EA 00 00 00 00 00   '.!......'
			 can0  516   [8]  06 61 EA 05 58 98 80 00   '.a..X...'
			 
			 [5] 05 58 98 80 00
			 */
			
			sendISOTPReply( WRANGLER_RADIO_REPLY, 0x21,  pid,  {0x05, 0x58, 0x98, 0x80} );
			break;
			
		default:
			break;
	}
}


// MARK: -   ISO-TP frame management

// handler frames larger than 8 bytes

bool	DTCManager::sendISOTPReply(canid_t can_id, uint8_t service_id, uint8_t pid,
											vector<uint8_t> bytes,
											int* error){
	
 	bytes.insert(bytes.begin(), pid);
  	return sendISOTPReply(can_id, service_id, bytes, error);
 }

bool	DTCManager::sendISOTPReply(canid_t can_id, uint8_t service_id,
											vector<uint8_t> bytes,
											int* error){
	PiCarCAN*	can 	= PiCarMgr::shared()->can();
	
	uint len = (uint)bytes.size();

	// is it a single frame?
	vector<uint8_t> data;
	data.reserve(len + 1);

	data.push_back(static_cast<uint8_t> ( service_id | 0x40));
	data.insert(data.end(), bytes.begin(), bytes.end());
	
	return can->sendISOTP(PiCarCAN::CAN_JEEP,  WRANGLER_RADIO_REPLY, data);
}
