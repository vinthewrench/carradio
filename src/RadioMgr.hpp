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
		RADIO_OFF,
		BROADCAST_AM,
		BROADCAST_FM,
		VHF,
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
	
	bool getDeviceInfo(RtlSdr::device_info_t&);
	
	static string freqSuffixString(double hz);
	static string hertz_to_string(double hz, int precision = 1);
	static string modeString(radio_mode_t);
	static radio_mode_t stringToMode(string);
	static string muxstring(radio_mux_t);
	
	 
	bool setFrequencyandMode(radio_mode_t, double freq = 0 );
	radio_mode_t radioMode() {return _mode;};
	double frequency();
	
	radio_mux_t radioMuxMode() {return _mux;};

	double nextFrequency(bool up);
		
private:


	bool					_isSetup;
	
	
	RtlSdr				_sdr;
	int					_pcmrate;
	radio_mode_t 		_mode;
	double				_frequency;
	radio_mux_t 		_mux;
	 
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

