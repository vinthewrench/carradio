

#if defined(__APPLE__)
#else
#include <alsa/asoundlib.h>
#endif

#include "AudioOutput.hpp"


AudioOutput *AudioOutput::sharedInstance = NULL;

AudioOutput::AudioOutput(){
	_isSetup = false;
	_pcm = NULL;
 
}

AudioOutput::~AudioOutput(){
	stop();
}

bool AudioOutput::begin(string devname,  unsigned int samplerate,  bool stereo){
	int error = 0;
	return begin(devname, samplerate,stereo, error);
}

 
bool AudioOutput::begin(string devname, unsigned int samplerate,  bool stereo,  int &error){
	
	bool success = false;
	
	
#if defined(__APPLE__)
	_isSetup = true;
	success = true;
#else
	int r;
	_pcm = NULL;
	r = snd_pcm_open(&_pcm, devname.c_str(),
								SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
	if( r < 0){
		error = r;
	}
	else {
		
		snd_pcm_nonblock(_pcm, 0);
		
		r = snd_pcm_set_params(m_pcm,
									  SND_PCM_FORMAT_S16_LE,
									  SND_PCM_ACCESS_RW_INTERLEAVED,
									  _nchannels,
									  samplerate,
									  1,               // allow soft resampling
									  500000);         // latency in us
		
		if( r < 0){
			error = r;
		} 	else {
			_isSetup = true;
			success = true;
			
		}
		
	}
	
#endif
	
	return success;
}

void AudioOutput::stop(){
	if(_isSetup){
	 
#if defined(__APPLE__)
#else

	 // Close device.
	 if (_pcm != NULL) {
		  snd_pcm_close(_pcm);
	 }
#endif

	};
	
	_isSetup = false;
}

 
