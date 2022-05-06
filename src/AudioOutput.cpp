

#if defined(__APPLE__)
#else
#include <alsa/asoundlib.h>
#endif

#include "AudioOutput.hpp"
#include "ErrorMgr.hpp"


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
	_nchannels = stereo ? 2 : 1;

	r = snd_pcm_open(&_pcm, devname.c_str(),
								SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
	if( r < 0){
		error = r;
	}
	else {
		
		snd_pcm_nonblock(_pcm, 0);
		
		r = snd_pcm_set_params(_pcm,
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

// Encode a list of samples as signed 16-bit little-endian integers.
void AudioOutput::samplesToInt16(const SampleVector& samples,
											vector<uint8_t>& bytes)
{
	 bytes.resize(2 * samples.size());

	 SampleVector::const_iterator i = samples.begin();
	 SampleVector::const_iterator n = samples.end();
	 vector<uint8_t>::iterator k = bytes.begin();

	 while (i != n) {
		  Sample s = *(i++);
		  s = max(Sample(-1.0), min(Sample(1.0), s));
		  long v = lrint(s * 32767);
		  unsigned long u = v;
		  *(k++) = u & 0xff;
		  *(k++) = (u >> 8) & 0xff;
	 }
}

 
bool AudioOutput::write(const SampleVector& samples)
{
	// Convert samples to bytes.
	samplesToInt16(samples, _bytebuf);

	// Write data.
	unsigned int p = 0;
	unsigned int n =  (unsigned int) samples.size() / _nchannels;
	unsigned int framesize = 2 * _nchannels;
	while (p < n) {
	
#if defined(__APPLE__)
#else
	 int k = snd_pcm_writei(_pcm, _bytebuf.data() + p * framesize, n - p);
		
		 if (k < 0) {
				 ELOG_ERROR(ErrorMgr::FAC_AUDIO, 0, errno, "write failed");
			 // After an underrun, ALSA keeps returning error codes until we
			  // explicitly fix the stream.
			  snd_pcm_recover(_pcm, k, 0);
			  return false;
			 
		 } else {
			  p += k;
		 }
#endif

	}
 	return true;
}
