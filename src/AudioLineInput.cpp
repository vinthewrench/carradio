//
//  AudioLineInput.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 7/9/22.
//

#include "AudioLineInput.hpp"
#include <stdint.h>

 #define _PCM_   "default"
 


AudioLineInput::AudioLineInput(){
  _isSetup = false;
   _pcm = NULL;
	_blockLength = default_blockLength;

 }

AudioLineInput::~AudioLineInput(){
	stop();
}

bool AudioLineInput::begin(unsigned int samplerate,  bool stereo){
	int error = 0;
	return begin(samplerate,stereo, error);
}


bool AudioLineInput::begin(unsigned int samplerate,  bool stereo,  int &error){
	
	bool success = false;
	
//	printf("AudioLineInput::begin\n");
	
	_pcm = NULL;
	_nchannels = stereo ? 2 : 1;
	
//	printf("AudioLineInput PCM at %d\n", samplerate);

#if defined(__APPLE__)
#else
	int r;
		
	r = snd_pcm_open(&_pcm, _PCM_,
						  SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
	if( r < 0){
		fprintf(stdout,  "input snd_pcm_open(%s) failed - %s \n", _PCM_, snd_strerror(r));
		error = r;
		return false;
	}
	
//	snd_pcm_nonblock(_pcm, 0);
	
	r = snd_pcm_set_params(_pcm,
								  SND_PCM_FORMAT_S16_LE,
								  SND_PCM_ACCESS_RW_INTERLEAVED,
								  _nchannels,
								  samplerate,
								  1,               // allow soft resampling
								  500000);         // latency in us
	if( r < 0){
		fprintf(stdout,  "input snd_pcm_set_params failed - %s \n",  snd_strerror(r));
		error = r;
		return false;
	}
	
	r =  snd_pcm_prepare(_pcm);
	if( r < 0){
		fprintf(stdout,  "cannot prepare audio interface for use - %s \n",  snd_strerror(r));
		error = r;
		return false;
	}

	r =  snd_pcm_start(_pcm);
	if( r < 0){
		fprintf(stdout,  "cannot start audio interface - %s \n",  snd_strerror(r));
		error = r;
		return false;
	}
	
	
 //	printf("AudioLineInput(%d) connected\n", samplerate);
	
#endif
	
	_isSetup = true;
	success = true;
	return success;
}

void AudioLineInput::stop(){
	if(_isSetup){
	 
//		printf("AudioLineInput::stop\n");
		
#if defined(__APPLE__)
#else

	 // Close device.
	 if (_pcm != NULL) {
//  printf("AudioLineInput closed\n");
		  snd_pcm_close(_pcm);
	 }
		
	 
#endif

	};
	
	_isSetup = false;
}


bool AudioLineInput::getSamples(SampleVector& audio){
	
	if(!_isSetup || !_pcm)
		return  false;
 
#if defined(__APPLE__)
#else
	
	int avail;
	int r;
	
	r =  snd_pcm_wait(_pcm, 1000);
	if( r < 0){
		return false;
	}
	
	avail = snd_pcm_avail_update(_pcm);
	if (avail > 0) {
		if (avail > _blockLength)
			avail = _blockLength;
		
		audio.resize(avail);

		int cnt =  snd_pcm_readi(_pcm,  audio.data(), avail);
		if(cnt > 0){
	 		return true;
		}
		
	}
		
#endif
 
	return false;
}

