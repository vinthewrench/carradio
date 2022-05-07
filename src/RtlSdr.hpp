//
//  RtlSdr.hpp
//  rtl
//
//  Created by Vincent Moscaritolo on 4/19/22.
//
//
// heavly derived from SoftFM sources by Joris van Rantwijk
//  https://github.com/jorisvr/SoftFM

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <complex>

#include "IQSample.h"
#include "CommonDefs.hpp"
#include "rtl-sdr.h"

using namespace std;


typedef std::complex<float> IQSample;
typedef std::vector<IQSample> IQSampleVector;

class RtlSdr
{
public:
	typedef struct  {
		uint32_t index;
		string 	name;
		string   vendor;
		string   product;
		string   serial;
	} device_info_t;

	static constexpr int 	default_blockLength = 65536;
	static constexpr double default_sampleRate = 1.0e6;

	RtlSdr();
	~RtlSdr();
	
	bool begin(uint32_t index, int &error);
	void stop();

	bool getDeviceInfo(device_info_t&);
 
	/** Set center frequency in Hz. */
	bool setFrequency(uint32_t);
 
	/** Return current center frequency in Hz. */
	uint32_t getFrequency();
	
	//Set the sample rate for the device, also selects the baseband filters
	// according to the requested sample rate for tuners where this is possible.
	bool setSampleRate(uint32_t);

	/** Return current sample frequency in Hz. */
	uint32_t getSampleRate();

	/** Return current tuner gain in units of 0.1 dB. */
	int getTunerGain();

	/** Return a list of supported tuner gain settings in units of 0.1 dB. */
	std::vector<int> getTunerGains();

	// set tuner gain in units of 0.1 dB. 
	bool setTunerGain(int);

	// set RTL AGC mode
	bool setACGMode(bool);

	// reset buffer to start streaming
	bool resetBuffer();
	
	/**
	 * Fetch a bunch of samples from the device.
	 *
	 * This function must be called regularly to maintain streaming.
	 * Return true for success, false if an error occurred.
	 */
	bool getSamples(IQSampleVector& samples);
 
	
//	/**
//	 * Configure RTL-SDR tuner and prepare for streaming.
//	 *
//	 * sample_rate  :: desired sample rate in Hz.
//	 * frequency    :: desired center frequency in Hz.
//	 * tuner_gain   :: desired tuner gain in 0.1 dB, or INT_MIN for auto-gain.
//	 * block_length :: preferred number of samples per block.
//	 *
//	 * Return true for success, false if an error occurred.
//	 */
//	bool configure(uint32_t sample_rate,
//						uint32_t frequency,
//						int tuner_gain,
//						int block_length=default_blockLength,
//						bool agcmode=false);
//
//
//
		/** Return a list of supported devices. */
	static std::vector<device_info_t> get_devices();

	/** Return a list of supported devices. */
	static std::vector<std::string> get_device_names();

private:
	
	bool						_isSetup;
	struct rtlsdr_dev * 	_dev;
	uint32_t 				_devIndex;
	int       				_blockLength;


};
