//
//  AudioLineInput.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 7/9/22.
//

#pragma once

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
 
#include "ErrorMgr.hpp"
#include "CommonDefs.hpp"
#include "IQSample.h"

using namespace std;

#if defined(__APPLE__)
#else
#include <alsa/asoundlib.h>
 
#endif
  
typedef  struct {
	int16_t ch1;
	int16_t ch2;
} audioData_t;
 
 typedef std::vector<audioData_t> AudioVector;

class AudioLineInput {
	 
public:
 
	static constexpr int 	default_blockLength = 4096;
	
	AudioLineInput();
	~AudioLineInput();
	
	bool begin(unsigned int samplerate = 48000,  bool stereo = true);
	bool begin(unsigned int samplerate,  bool stereo,  int &error);
	void stop();
	bool iConnected() { return _isSetup; }
 
	bool getSamples(SampleVector& audio);
	
	bool getAudio(AudioVector& audio);

	
	private:
 
	bool						_isSetup;
	unsigned int         _nchannels;
	struct _snd_pcm *   	_pcm;
	 
	int       				_blockLength;

  };

