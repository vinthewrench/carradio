
#include "AudioOutput.hpp"
#include "ErrorMgr.hpp"
#include <math.h>
#include <stdbool.h>

#include <stdio.h>
#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>

 AudioOutput::AudioOutput(){
	_isSetup = false;
	_balance = 0;
	_pcm = NULL;
 
}

AudioOutput::~AudioOutput(){
	stop();
}

bool AudioOutput::begin(const char* path,  unsigned int samplerate,  bool stereo){
	int error = 0;
	return begin(path, samplerate,stereo, error);
}

 
bool AudioOutput::begin(const char* path, unsigned int samplerate,  bool stereo,  int &error){
	
	bool success = false;
	
	_pcm = NULL;
	_nchannels = stereo ? 2 : 1;

#if defined(__APPLE__)
	_isSetup = true;
	success = true;
#else
	int r;
 
	r = snd_pcm_open(&_pcm, path,
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
	
	
#if defined(__APPLE__)
	
	fprintf(stderr,"Output %ld samples\n", samples.size());
#else
	// Write data.
	unsigned int p = 0;
	unsigned int n =  (unsigned int) samples.size() / _nchannels;
	unsigned int framesize = 2 * _nchannels;

	while (p < n) {
		int k = snd_pcm_writei(_pcm, _bytebuf.data() + p * framesize, n - p);
		
		if (k < 0) {
	//		ELOG_ERROR(ErrorMgr::FAC_AUDIO, 0, errno, "write failed");
			// After an underrun, ALSA keeps returning error codes until we
			// explicitly fix the stream.
			snd_pcm_recover(_pcm, k, 0);
			return false;
			
		} else {
			p += k;
		}
	}
#endif
	
	
	return true;
}
/*
void AudioOutput::test(char* fname){
#if defined(__APPLE__)

#else
	unsigned int pcm, tmp, dir;
	unsigned int rate, channels, seconds;
	snd_pcm_t *pcm_handle;
	snd_pcm_hw_params_t *params;
	snd_pcm_uframes_t frames;
	char *buff;
	int buff_size, loops;

	rate = 44100;
	channels = 2;
	seconds = 5;

	int fd = open(fname, O_RDONLY);   ///added
	if (fd <= 0) {
		printf("unable to open file '%s'!\n", fname);
		return;
	}
		
	 lseek(fd,44, SEEK_SET);
 
//Allocate parameters object and fill it with default values
	snd_pcm_hw_params_alloca(&params);

	snd_pcm_hw_params_any(_pcm, params);

 
 
	if (pcm = snd_pcm_hw_params_set_access(_pcm, params,
					SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
		printf("ERROR: Can't set interleaved mode. %s\n", snd_strerror(pcm));

	if (pcm = snd_pcm_hw_params_set_format(_pcm, params,
						SND_PCM_FORMAT_S16_LE) < 0)
		printf("ERROR: Can't set format. %s\n", snd_strerror(pcm));

	if (pcm = snd_pcm_hw_params_set_channels(_pcm, params, channels) < 0)
		printf("ERROR: Can't set channels number. %s\n", snd_strerror(pcm));

	if (pcm = snd_pcm_hw_params_set_rate_near(_pcm, params, &rate, 0) < 0)
		printf("ERROR: Can't set rate. %s\n", snd_strerror(pcm));

 
	if (pcm = snd_pcm_hw_params(_pcm, params) < 0)
		printf("ERROR: Can't set harware parameters. %s\n", snd_strerror(pcm));

 	printf("PCM name: '%s'\n", snd_pcm_name(_pcm));

	printf("PCM state: %s\n", snd_pcm_state_name(snd_pcm_state(_pcm)));

	snd_pcm_hw_params_get_channels(params, &tmp);
	printf("channels: %i ", tmp);

	if (tmp == 1)
		printf("(mono)\n");
	else if (tmp == 2)
		printf("(stereo)\n");

	snd_pcm_hw_params_get_rate(params, &tmp, 0);
	printf("rate: %d bps\n", tmp);

	printf("seconds: %d\n", seconds);

 	snd_pcm_hw_params_get_period_size(params, &frames, 0);

	buff_size = frames * channels * 2;  // 2 -> sample size * 
	buff = (char *) malloc(buff_size);

	snd_pcm_hw_params_get_period_time(params, &tmp, NULL);

	for (loops = (seconds * 1000000) / tmp; loops > 0; loops--) {

		if (pcm = read(fd, buff, buff_size) == 0) {
			printf("Early end of file.\n");
			return ;
		}
		printf("read: %d\n", frames);



		if (pcm = snd_pcm_writei(_pcm, buff, frames) == -EPIPE) {
			printf("XRUN.\n");
			snd_pcm_prepare(_pcm);
		} else if (pcm < 0) {
			printf("ERROR. Can't write to PCM device. %s\n", snd_strerror(pcm));
		}

	}

	snd_pcm_drain(_pcm);
	snd_pcm_close(pcm_handle);
	free(buff);

 
	close(fd);
#endif
}

*/


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

bool 	AudioOutput::setVolume(double volIn){

	volIn = fmax(0, fmin(1, volIn));  // pin volume
	
	double left =  volIn;
	double right  =  volIn;
 
	double adjusted =  volIn * (1 - fabs(_balance));

	if( _balance > 0) {
		left = adjusted;
	}else if( _balance < 0) {
		right = adjusted;
	}
	set_normalized_volume(_elem, SND_MIXER_SCHN_FRONT_RIGHT, right ,0, PLAYBACK);
	set_normalized_volume(_elem, SND_MIXER_SCHN_FRONT_LEFT, left ,0, PLAYBACK);

	return true;
}

double AudioOutput::volume() {
	double left = get_normalized_volume(_elem, SND_MIXER_SCHN_FRONT_LEFT, PLAYBACK);
	double right = get_normalized_volume(_elem, SND_MIXER_SCHN_FRONT_RIGHT,PLAYBACK);
	
	return fmax(left, right);
 }



#endif

bool AudioOutput::setBalance(double newBal) {

	newBal = fmax(-1, fmin(1, newBal));  // pin balance

	_balance = newBal;
	return setVolume(volume());
}


double AudioOutput::balance() {
	return _balance;
}

