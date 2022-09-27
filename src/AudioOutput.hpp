

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

#if defined(__APPLE__)
typedef struct _snd_mixer snd_mixer_t;
typedef struct _snd_mixer_elem snd_mixer_elem_t;
#else
#include <alsa/asoundlib.h>
#include <alsa/mixer.h>

#endif
 

class AudioOutput {
	
public:
	
	AudioOutput();
	~AudioOutput();
	
	bool begin(unsigned int samplerate = 48000,  bool stereo = true);
	bool begin(unsigned int samplerate,  bool stereo,  int &error);
	void stop();
	
	bool writeAudio(const SampleVector& samples);
	bool writeIQ(const SampleVector& samples);
	
	bool 	setVolume(double );		// 0.0 - 1.0  % of max
	double volume();
	
	bool setMute(bool shouldMute);
//	bool isMuted() {return (_isMuted && (_savedVolume > 0));};
	bool isMuted() {return (_isMuted);};
	double mutedVolume() { return _savedVolume; };
	
	
	bool 	setBalance(double );		// -1.0  - 1.0
	double balance();
	
	bool 	setFader(double );		// -1.0  - 1.0
	double fader();
	
	
	//	bool playSound(string filePath, boolCallback_t cb);
	
private:
	
	bool						_isSetup;
	unsigned int         _nchannels;
	struct _snd_pcm *   	_pcm;
	
	snd_mixer_t* 			_mixer;
	snd_mixer_elem_t* 	_volume;
	
	double					_balance;
	double					_fader;
	
	double					_savedVolume;
	bool						_isMuted = false;
	
	vector<uint8_t>  		_bytebuf;
	
	void  samplesToInt16(const SampleVector& samples, vector<uint8_t>& bytes);
};

