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

#include "DataBuffer.hpp"
#include "ErrorMgr.hpp"
#include "CommonDefs.hpp"
#include "FmDecode.hpp"

using namespace std;


class RadioMgr {
	
public:
	typedef enum :int {
		MODE_UNKNOWN = 0,
		BROADCAST_AM,
		BROADCAST_FM,
		VHF,
		GMRS,
		LINE_IN,  // not really a radio option.
	}radio_mode_t;
	
	typedef enum  {
		MUX_UNKNOWN = 0,
		MUX_MONO,
		MUX_STEREO,
		MUX_QUAD,
	}radio_mux_t;
	
 
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
	radio_mode_t radioMode() {return _mode;};
	uint32_t frequency();
	
	radio_mux_t radioMuxMode() {return _mux;};

	uint32_t nextFrequency(bool up);
	
//	uint32_t nextFrequency(bool up, bool constrain = false);
	

private:


	bool					_isSetup;
	
	
	RtlSdr				_sdr;
	int					_pcmrate;
	radio_mode_t 		_mode;
	uint32_t				_frequency;
	radio_mux_t 		_mux;
	bool					_isOn;
	 
	// Create source data queue.
	DataBuffer<IQSample> _source_buffer;
	
	// output data queue
	DataBuffer<Sample>   _output_buffer;

	mutable std::mutex _mutex;		// when changing frequencies and modes.
	FmDecoder*			_fmDecoder;
	
	// SDR Reader thread
	bool					 _shouldQuit;
	bool					 _shouldRead;

	pthread_t			_sdrReaderTID;
	pthread_t			_sdrProcessorTID;
	pthread_t			_outputProcessorTID;
	
	void SDRReader();		// C++ version of thread
	// C wrappers for SDRReader;
	static void* SDRReaderThread(void *context);
	static void SDRReaderThreadCleanup(void *context);

	
	void SDRProcessor();		// C++ version of thread
	// C wrappers for SDRReader;
	static void* SDRProcessorThread(void *context);
	static void SDRProcessorThreadCleanup(void *context);

	
	void OutputProcessor();		// C++ version of thread
	// C wrappers for SDRReader;
	static void* OutputProcessorThread(void *context);
	static void OutputProcessorThreadCleanup(void *context);

};

