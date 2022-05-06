
#include "AudioOutput.hpp"
#include "ErrorMgr.hpp"
#include <math.h>
#include <stdbool.h>


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
			
			snd_mixer_open(&_mixer , SND_MIXER_ELEM_SIMPLE);
			snd_mixer_attach(_mixer, "hw:1");
			snd_mixer_selem_register(_mixer, NULL, NULL);
			snd_mixer_load(_mixer);
			snd_mixer_handle_events(_mixer);

			snd_mixer_selem_id_t *sid;
			snd_mixer_selem_id_alloca(&sid);
			snd_mixer_selem_id_set_index(sid, 0);
			snd_mixer_selem_id_set_name(sid, "Speaker");
		 
			_elem = snd_mixer_find_selem(_mixer, sid);
			
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
		
		snd_mixer_detach(_mixer, "hw:0");
		snd_mixer_close(_mixer);

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
		
		framesize = framesize;
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



// MARK: -  Mixer Volume
#if defined(__APPLE__)

bool 	AudioOutput::setVolume(double newVol){
	
	return true;
}

double AudioOutput::volume() {
	return 50. ;
}

#else

#define MAX_LINEAR_DB_SCALE     24

static inline bool use_linear_dB_scale(long dBmin, long dBmax)
{
		  return dBmax - dBmin <= MAX_LINEAR_DB_SCALE * 100;
}

static long lrint_dir(double x, int dir)
{
		  if (dir > 0)
					 return lrint(ceil(x));
		  else if (dir < 0)
					 return lrint(floor(x));
		  else
					 return lrint(x);
}

enum ctl_dir { PLAYBACK, CAPTURE };

static int (* const get_dB_range[2])(snd_mixer_elem_t *, long *, long *) = {
		  snd_mixer_selem_get_playback_dB_range,
		  snd_mixer_selem_get_capture_dB_range,
};
static int (* const get_raw_range[2])(snd_mixer_elem_t *, long *, long *) = {
		  snd_mixer_selem_get_playback_volume_range,
		  snd_mixer_selem_get_capture_volume_range,
};
static int (* const get_dB[2])(snd_mixer_elem_t *, snd_mixer_selem_channel_id_t, long *) = {
		  snd_mixer_selem_get_playback_dB,
		  snd_mixer_selem_get_capture_dB,
};
static int (* const get_raw[2])(snd_mixer_elem_t *, snd_mixer_selem_channel_id_t, long *) = {
		  snd_mixer_selem_get_playback_volume,
		  snd_mixer_selem_get_capture_volume,
};
static int (* const set_dB[2])(snd_mixer_elem_t *, snd_mixer_selem_channel_id_t, long, int) = {
		  snd_mixer_selem_set_playback_dB,
		  snd_mixer_selem_set_capture_dB,
};
static int (* const set_raw[2])(snd_mixer_elem_t *, snd_mixer_selem_channel_id_t, long) = {
		  snd_mixer_selem_set_playback_volume,
		  snd_mixer_selem_set_capture_volume,
};

static double get_normalized_volume(snd_mixer_elem_t *elem,
												snd_mixer_selem_channel_id_t channel,
												enum ctl_dir ctl_dir)
{
		  long min, max, value;
		  double normalized, min_norm;
		  int err;

		  err = get_dB_range[ctl_dir](elem, &min, &max);
		  if (err < 0 || min >= max) {
					 err = get_raw_range[ctl_dir](elem, &min, &max);
					 if (err < 0 || min == max)
								return 0;

					 err = get_raw[ctl_dir](elem, channel, &value);
					 if (err < 0)
								return 0;

					 return (value - min) / (double)(max - min);
		  }

		  err = get_dB[ctl_dir](elem, channel, &value);
		  if (err < 0)
					 return 0;

		  if (use_linear_dB_scale(min, max))
					 return (value - min) / (double)(max - min);

		  normalized = pow(10, (value - max) / 6000.0);
		  if (min != SND_CTL_TLV_DB_GAIN_MUTE) {
					 min_norm = pow(10, (min - max) / 6000.0);
					 normalized = (normalized - min_norm) / (1 - min_norm);
		  }

		  return normalized;
}

static int set_normalized_volume(snd_mixer_elem_t *elem,
											snd_mixer_selem_channel_id_t channel,
											double volume,
											int dir,
											enum ctl_dir ctl_dir)
{
		  long min, max, value;
		  double min_norm;
		  int err;

		  err = get_dB_range[ctl_dir](elem, &min, &max);
		  if (err < 0 || min >= max) {
					 err = get_raw_range[ctl_dir](elem, &min, &max);
					 if (err < 0)
								return err;

					 value = lrint_dir(volume * (max - min), dir) + min;
					 return set_raw[ctl_dir](elem, channel, value);
		  }

		  if (use_linear_dB_scale(min, max)) {
					 value = lrint_dir(volume * (max - min), dir) + min;
					 return set_dB[ctl_dir](elem, channel, value, dir);
		  }

		  if (min != SND_CTL_TLV_DB_GAIN_MUTE) {
					 min_norm = pow(10, (min - max) / 6000.0);
					 volume = volume * (1 - min_norm) + min_norm;
		  }
		  value = lrint_dir(6000.0 * log10(volume), dir) + max;
		  return set_dB[ctl_dir](elem, channel, value, dir);
}

bool 	AudioOutput::setVolume(double volume){
	
	set_normalized_volume(_elem, SND_MIXER_SCHN_FRONT_RIGHT, volume ,0, PLAYBACK);
	set_normalized_volume(_elem, SND_MIXER_SCHN_FRONT_LEFT, volume ,0, PLAYBACK);

	return true;
}

double AudioOutput::volume() {
	
	double left = get_normalized_volume(_elem, SND_MIXER_SCHN_FRONT_LEFT, PLAYBACK);
	double right = get_normalized_volume(_elem, SND_MIXER_SCHN_FRONT_RIGHT,PLAYBACK);
	printf("L: %f R: %f\n", left*100, right*100);
  
	
	return 50. ;
}

#endif
