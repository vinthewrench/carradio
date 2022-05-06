

#pragma once

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "IQSample.h"
#include "RtlSdr.hpp"

#include "ErrorMgr.hpp"
#include "CommonDefs.hpp"

using namespace std;
 

class AudioOutput {
	 
public:
	  static AudioOutput *shared() {
		  if(!sharedInstance){
			  sharedInstance = new AudioOutput;
		  }
		  return sharedInstance;
	  }

	AudioOutput();
	~AudioOutput();
	
	bool begin(string devname = "default",  unsigned int samplerate = 48000,  bool stereo = true);
	bool begin(string devname, unsigned int samplerate,  bool stereo,  int &error);
	void stop();
 
	bool write(const SampleVector& samples);
	
	private:
 
	bool						_isSetup;
	unsigned int         _nchannels;
	struct _snd_pcm *   	_pcm;
	vector<uint8_t>  		_bytebuf;
 
	static AudioOutput *sharedInstance;
 
	void  samplesToInt16(const SampleVector& samples, vector<uint8_t>& bytes);
};

