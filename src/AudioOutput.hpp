

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
 
	 
	
	private:
 
	bool						_isSetup;
	unsigned int         _nchannels;
	struct _snd_pcm *   	_pcm;

	static AudioOutput *sharedInstance;
 
};

