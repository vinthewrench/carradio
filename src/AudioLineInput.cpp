//
//  AudioLineInput.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 7/9/22.
//

#include "AudioLineInput.hpp"



#define _PCM_   "default"



AudioLineInput::AudioLineInput(){
  _isSetup = false;
   _pcm = NULL;
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
	
	_pcm = NULL;
	_nchannels = stereo ? 2 : 1;
	
#if defined(__APPLE__)
#else
	int r;
	
	int r;
	
	r = snd_pcm_open(&_pcm, _PCM_,
						  SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
	if( r < 0){
		fprintf(stderr,  "input snd_pcm_open(%s) failed - %s \n", _PCM_, snd_strerror(r));
		error = r;
		return false;
	}
	
	snd_pcm_nonblock(_pcm, 0);
	
	r = snd_pcm_set_params(_pcm,
								  SND_PCM_FORMAT_S16_LE,
								  SND_PCM_ACCESS_RW_INTERLEAVED,
								  _nchannels,
								  samplerate,
								  1,               // allow soft resampling
								  500000);         // latency in us
	if( r < 0){
		fprintf(stderr,  "input snd_pcm_set_params failed - %s \n",  snd_strerror(r));
		error = r;
		return false;
	}
	
	printf("AudioLineInput connected\n");
	
#endif
	
	_isSetup = true;
	success = true;
	return success;
}

void AudioLineInput::stop(){
	if(_isSetup){
	 
#if defined(__APPLE__)
#else

	 // Close device.
	 if (_pcm != NULL) {
		 
		 printf("AudioLineInput closed\n");

		  snd_pcm_close(_pcm);
	 }
		
	 
#endif

	};
	
	_isSetup = false;
}
