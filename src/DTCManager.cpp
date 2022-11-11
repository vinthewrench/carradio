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
	_multi_frame.clear();
}

DTCManager::~DTCManager(){
	_isSetup = false;

 }


bool DTCManager::begin( ){
 
	bool status = false;
 	_isSetup = false;
 
	// register Wangler Radio Frame Handler
	PiCarCAN*	can 	= PiCarMgr::shared()->can();
	status = can->registerFrameHandler( PiCarCAN::CAN_JEEP, WRANGLER_RADIO_REQ, processWanglerRadioFrameWrapper, this);
 
	_isSetup = status;
	return true;
 }

 
void DTCManager::stop(){
	
	if(_isSetup  ){
		PiCarCAN*	can 	= PiCarMgr::shared()->can();
		can->unRegisterFrameHandler(PiCarCAN::CAN_JEEP, WRANGLER_RADIO_REQ, processWanglerRadioFrameWrapper);
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
			
		case 3:  //  flow control C  frame
		 
		  //	guard code only handle Flow control frame (FC)
			if(frame.len < 3)  return;
 			processISOTPFlowControlFrame(timeStamp, can_id, frame.data);
 	 		break;
			
		default: ;
		 // we only handle single frame message requests
	}

}

void	DTCManager::processPrivateODB(time_t when,  canid_t can_id, uint8_t service_id,
												uint16_t len, uint8_t* data){
	
	// mode 21 and 3e/01
	
	switch (service_id) {
			
		case 0x3E:		// heartbeat?
			if( len > 0 && data[0] == 0x01){
	 			// send reply as raw frame
				PiCarCAN*	can 	= PiCarMgr::shared()->can();
				can->sendFrame(PiCarCAN::CAN_JEEP,  WRANGLER_RADIO_REPLY, {0x01, 0x7E, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00}, NULL);
			}
			break;
	
		case 0x1A:
			if( len > 0) {
				processWanglerRadioPID1A(data[0]);
			}
			break;

		case 0x21:
			if( len > 0) {
				processWanglerRadioPID21(data[0]);
			}
			break;

		case 0x18:
			if( len > 0) {
				processWanglerRadioPID18(data[0], len -1, data+1);
			}
			break;

		default:
			break;
	}

}

void	DTCManager::processWanglerRadioPID18(uint8_t pid, 	uint16_t len, uint8_t* data){
	
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
 		sendISOTPReply( WRANGLER_RADIO_REPLY, 0x18, {0x02, 0x94,0x86,0x60,0x94,0x81,0x60});

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
			 516#1010 61E154313141	'..a.T11A'
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

	bool success = false;
	
	uint len = (uint)bytes.size();

// debug
	{
		printf("send  %03x [%2d] sid:%02x: |", can_id, (int) len + 1, service_id);
		for(int i = 0; i < len; i++) printf("%02x ", bytes[i]);
		printf("|\n");
	}
	
	if(len < 6){
		// is it a single frame?
		vector<uint8_t> data;
		data.reserve(len + 2);
 
		data.push_back(static_cast<uint8_t> ( len & 0x0f));
		data.push_back(static_cast<uint8_t> ( service_id & 0x40));
	 	data.insert(data.end(), bytes.begin(), bytes.end());
		can->sendFrame(PiCarCAN::CAN_JEEP,  WRANGLER_RADIO_REPLY, data);
 
	}
	else {
		// multi frame
	}
	
/* debug with
 	candump can0,6b0:7ff,516:7ff -a
 
 
 cansend can0 6B0#023e010000000000
 cansend can0 6B0#041800FF00000000
 cansend can0 6B0#021A870000000000
 cansend can0 6B0#0221E10000000000
*/
	
	return success;
 }

 
void	DTCManager::processISOTPFlowControlFrame(time_t when,  canid_t can_id,  uint8_t* data){
 
	uint8_t fc_flag =  data[0] & 0x4;
//  0 = Continue To Send,
//	 1 = Wait,
//	 2 = Overflow/abort
	
	uint8_t block_size =  data[1];
 	uint8_t ST =  data[2];

#warning write code to process multi frame 
	/*
	 The initial byte contains the type (type = 3) in the first four bits,
	 and a flag in the next four bits indicating if the transfer is allowed
	 (0 = Clear To Send,
	 1 = Wait,
	 2 = Overflow/abort).
	 The next byte is the block size, the count of frames that may be sent before waiting for the next flow control frame.
	 A value of zero allows the remaining frames to be sent without flow control or delay.
	 
	 The third byte is the Separation Time (ST), the minimum delay time between frames.
	 ST values up to 127 (0x7F) specify the minimum number of milliseconds to delay between frames,
	 while values in the range 241 (0xF1) to 249 (0xF9) specify delays increasing from 100 to 900 microseconds.
	 
	 Note that the Separation Time is defined as the minimum time between the end of one frame to the beginning of the next.
	 Robust implementations should be prepared to accept frames from a sender that misinterprets this as the
	 frame repetition rate i.e. from start-of-frame to start-of-frame.
	 Even careful implementations may fail to account for the minor effect of bit-stuffing in the physical layer.

	The sender transmits the rest of the message using Consecutive Frames.
	 Each Consecutive Frame has a one byte PCI, with a four bit type (type = 2) followed by a 4-bit sequence number.
	 The sequence number starts at 1 and increments with each frame sent (1, 2,..., 15, 0, 1,...),
	 with which lost or discarded frames can be detected.
	 
	 Each consecutive frame starts at 0, initially for the first set of data in the first frame will be considered as 0th data.
	 So the first set of CF(Consecutive frames) start from "1".
	 There afterwards when it reaches "15", will be started from "0".
	 The 12 bit length field (in the FF) allows up to 4095 bytes of user data in a segmented message,
	 but in practice the typical application-specific limit is considerably lower because of receive buffer or hardware limitations.
	 */
	
}
