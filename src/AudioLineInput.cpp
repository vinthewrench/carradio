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
	
	_pcm = NULL;
	_nchannels = stereo ? 2 : 1;
	
#if defined(__APPLE__)
#else
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
	
	r =  snd_pcm_prepare(_pcm);
	if( r < 0){
		fprintf(stderr,  "cannot prepare audio interface for use - %s \n",  snd_strerror(r));
		error = r;
		return false;
	}

	r =  snd_pcm_start(_pcm);
	if( r < 0){
		fprintf(stderr,  "cannot start audio interface - %s \n",  snd_strerror(r));
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


bool AudioLineInput::getSamples(){
	
	if(!_isSetup || !_pcm)
		return  false;
	
	vector<uint8_t> buf(2 * _blockLength);
	
#if defined(__APPLE__)
#else
	
	int avail;
	int r;
	
	r =  snd_pcm_wait(_pcm, 1000);
	if( r < 0){
		fprintf(stderr,  "snd_pcm_wait - %s \n",  snd_strerror(r));
		return false;
	}
	
	avail = snd_pcm_avail_update(_pcm);
	if (avail > 0) {
		if (avail > _blockLength)
			avail = _blockLength;
		
		int cnt =  snd_pcm_readi(_pcm,  buf.data(), avail);
		if(cnt > 0)
			printf("%d frames read \n", cnt);
	}
	
	avail = snd_pcm_avail_update(_pcm);
	if (avail > 0) {
		if (avail > _blockLength)
			avail = _blockLength;
		
		printf("%d bytes avail\n", avail);
	}
	
#endif
	
	
	return true;
}

