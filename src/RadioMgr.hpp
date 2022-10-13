//
//  RadioMgr.hpp
//  vfdtest
//
//  Created by Vincent Moscaritolo on 5/4/22.
//

#pragma once

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <mutex>
#include <bitset>
#include <time.h>
#include <unistd.h>


#include <sys/time.h>
#include "RtlSdr.hpp"
#include "SDRDecoder.hpp"

#include "DataBuffer.hpp"
#include "ErrorMgr.hpp"
#include "CommonDefs.hpp"
#include "AudioLineInput.hpp"
#include "AirplayInput.hpp"

using namespace std;


class RadioMgr {
	
public:
	typedef enum :int {
		MODE_UNKNOWN = 0,
		BROADCAST_AM,
		BROADCAST_FM,
		VHF,
		GMRS,
		SCANNER,
		AUX,
		AIRPLAY
	}radio_mode_t;
	
	typedef enum  {
		MUX_UNKNOWN = 0,
		MUX_MONO,
		MUX_STEREO,
		MUX_QUAD,
	}radio_mux_t;
	
 
	typedef pair<RadioMgr::radio_mode_t,uint32_t> channel_t;
	 
	RadioMgr();
	~RadioMgr();
	
	bool begin(uint32_t deviceIndex  = 0, int  pcmrate = 48000);
	bool begin(uint32_t deviceIndex, int  pcmrate,  int &error);
	void stop();
	
	bool isConnected() ;

	bool getDeviceInfo(RtlSdr::device_info_t&);
	
	static string freqSuffixString(double hz);
	static string hertz_to_string(double hz, int precision = 1);
	static string modeString(radio_mode_t);
	static radio_mode_t stringToMode(string);
	static string muxstring(radio_mux_t);
	static bool freqRangeOfMode(radio_mode_t mode, uint32_t & minFreq,  uint32_t &maxFreq);

 	bool setON(bool);
	bool isOn() {return _isOn;};
	
	bool setFrequencyandMode(radio_mode_t, uint32_t freq = 0, bool force = false);
	radio_mode_t radioMode();
	uint32_t frequency();
	
	bool scanChannels( vector < RadioMgr::channel_t >  channels );
	void pauseScan(bool shouldPause);
	
	vector < RadioMgr::channel_t >  scannerChannels();
 	bool getCurrentScannerChannel(radio_mode_t &mode, uint32_t &freq);
	bool tuneNextScannerChannel();
 
	bool isScannerMode(){return _scannerMode;};
	bool scannerLocked();
	
	radio_mux_t radioMuxMode() {return _mux;};

	double get_if_level() {return _IF_Level;};
	double get_baseband_level() {return _baseband_level;};
	bool  isSquelched();
 
	void 	setSquelchLevel(int level);
	int 	getSquelchLevel(){ return _squelchLevel;};
	int 	getMaxSquelchRange();
	void 	setSquelchDwell(uint val){ _squelchLevel = val;};
	 
	uint32_t nextFrequency(bool up);
	
	bool tuneScannerToChannel(RadioMgr::channel_t);
//	uint32_t nextFrequency(bool up, bool constrain = false);
	

private:


	bool					_isSetup;
	
	
	RtlSdr				_sdr;
	int					_pcmrate;
	radio_mode_t 		_mode;
	uint32_t				_frequency;
	radio_mux_t 		_mux;
	int					_squelchLevel;
		
	double				_IF_Level;
	double 				_baseband_level;
	
	bool					_isOn;
	 
	// Create source data queue.
	DataBuffer<IQSample> _source_buffer;
	
	// output data queue
	DataBuffer<Sample>   _output_buffer;


 	mutable std::mutex _mutex;		// when changing frequencies and modes.
	SDRDecoder*			_sdrDecoder;
	AudioLineInput		_lineInput;
	AirplayInput		_airplayInput;
 
	vector < RadioMgr::channel_t > _scannerChannels;
	uint									_currentScanOffset;
	bool									_scannerMode;
	bool									_scanningPaused;
 
	bool setFrequencyandModeInternal(radio_mode_t, uint32_t freq = 0, bool force = false);

	void queueSetFrequencyandMode(radio_mode_t, uint32_t freq = 0, bool force = false);

	//  Reader threads
	bool					 _shouldQuit;
	bool					 _shouldReadSDR;
	bool					 _shouldReadAux;
	bool					 _shouldReadAirplay;
  
	pthread_t			_auxReaderTID;
	pthread_t			_sdrReaderTID;
	pthread_t			_sdrProcessorTID;
	pthread_t			_outputProcessorTID;
	pthread_t			_channelManagerTID;
	pthread_t			_airplayReaderTID;

	void SDRReader();		// C++ version of thread
	static void* SDRReaderThread(void *context);
	static void SDRReaderThreadCleanup(void *context);
	
	void AuxReader();		// C++ version of thread
	static void* AuxReaderThread(void *context);
	static void AuxReaderThreadCleanup(void *context);
	
	void AirplayReader();		// C++ version of thread
	static void* AirplayReaderThread(void *context);
	static void AirplayReaderCleanup(void *context);

	void SDRProcessor();		// C++ version of thread
	static void* SDRProcessorThread(void *context);
	static void SDRProcessorThreadCleanup(void *context);
	
	void OutputProcessor();		// C++ version of thread
	static void* OutputProcessorThread(void *context);
	static void OutputProcessorThreadCleanup(void *context);
 
	void ChannelManager();		// C++ version of thread
	static void* ChannelManagerThread(void *context);
	static void ChannelManagerThreadCleanup(void *context);

	
	  typedef struct {
		  radio_mode_t  mode;
		  uint32_t 		freq;
		  bool			force;
	  }  channelEventQueueItem_t;

	  queue<channelEventQueueItem_t> _channelEventQueue; // upper 8 bits is mode . lower 8 is event

	pthread_cond_t 	_channelCond = PTHREAD_COND_INITIALIZER;
	pthread_mutex_t 	_channelmutex = PTHREAD_MUTEX_INITIALIZER;

};

