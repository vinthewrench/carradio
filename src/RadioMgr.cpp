//
//  RadioMgr.cpp
//  vfdtest
//
//  Created by Vincent Moscaritolo on 5/4/22.
//

#include "RadioMgr.hpp"
#include "RtlSdr.hpp"
#include <time.h>
#include <sys/time.h>

#include <cmath>
#include <limits.h>

#include "PiCarMgr.hpp"
#include "DisplayMgr.hpp"
#include "AudioOutput.hpp"
#include "PropValKeys.hpp"

#define DEBUG_DEMOD 1
typedef void * (*THREADFUNCPTR)(void *);


RadioMgr::RadioMgr(){
	_mode = MODE_UNKNOWN;
	_mux = MUX_MONO;
	_fmDecoder = NULL;
	_frequency = 0;
	_isOn = false;
	_isSetup = false;
	
	_shouldQuit = false;
 
 }
 
RadioMgr::~RadioMgr(){
	stop();
	}
 

bool RadioMgr::begin(uint32_t deviceIndex, int  pcmrate){
	int error = 0;

	return begin(deviceIndex,pcmrate,  error);
}

bool RadioMgr::begin(uint32_t deviceIndex, int  pcmrate,  int &error){
	
	_isSetup = false;
	_pcmrate = pcmrate;
	
	if(! _sdr.begin(deviceIndex,error ) )
		return false;

	// tSet Sample rate
	if(! _sdr.setSampleRate(RtlSdr::default_sampleRate))
		return false;
	
	// start with auto gain
	if(! _sdr.setTunerGain( INT_MIN ))
		return false;
	
	// turn of ACG
	if(! _sdr.setACGMode(false))
		return false;
  
	pthread_create(&_sdrReaderTID, NULL,
								  (THREADFUNCPTR) &RadioMgr::SDRReaderThread, (void*)this);

	pthread_create(&_sdrProcessorTID, NULL,
								  (THREADFUNCPTR) &RadioMgr::SDRProcessorThread, (void*)this);


	pthread_create(&_outputProcessorTID, NULL,
								  (THREADFUNCPTR) &RadioMgr::OutputProcessorThread, (void*)this);


	_isSetup = true;
 
 	return true;
}

void RadioMgr::stop(){
	
	if(_isSetup  ){
		_shouldRead = false;
		_shouldQuit = true;
		pthread_join(_sdrReaderTID, NULL);
		pthread_join(_sdrProcessorTID, NULL);
		pthread_join(_outputProcessorTID, NULL);
		_sdr.stop();
 	}
	
 	_isSetup = false;
 
 }



bool RadioMgr::getDeviceInfo(RtlSdr::device_info_t& info){
	return _sdr.getDeviceInfo(info);
}

bool RadioMgr::setON(bool isOn) {
	
	DisplayMgr*		display 	= PiCarMgr::shared()->display();
	PiCarDB*			db 		= PiCarMgr::shared()->db();
	
	if(!_isSetup)
		return false;
	
	if(isOn == _isOn)
		return true;
	
	_isOn = isOn;
	
	db->updateValue(VAL_RADIO_ON, isOn);
	
	if(!isOn){
		std::lock_guard<std::mutex> lock(_mutex);
		
		_shouldRead = false;
		_sdr.resetBuffer();
		_output_buffer.flush();
		
		// delete decoders
		if(_fmDecoder) {
			delete _fmDecoder;
			_fmDecoder = NULL;
		}
		display->showTime();
	}
	else {
		setFrequencyandMode(_mode,_frequency, true);
		display->showRadioChange();
	}
	
	return true;
}


// MARK: -  utilities

//uint32_t RadioMgr::stringToFreq(string str ){
//
//	uint32_t  freq =0;
//	uint32_t  suff = 1;
//	
//	auto suffix = str.back();
//	switch (suffix) {
//		case 'g':
//		case 'G':
//			suff *= 1e3;
//			/* fall-through */
//		case 'm':
//		case 'M':
//			suff *= 1e3;
//			/* fall-through */
//		case 'k':
//		case 'K':
//			suff *= 1e3;
//			str.pop_back();
//		default:
// 			freq  = std::stof(str) * suff;
//			break;
//	}
//	
//	return freq;
//}

bool RadioMgr::setFrequencyandMode( radio_mode_t newMode, uint32_t newFreq, bool force){
	
 
	DisplayMgr*		display 	= PiCarMgr::shared()->display();
	PiCarDB*			db 		= PiCarMgr::shared()->db();
	bool 			didUpdate = false;
	
	if(!_isSetup)
		return false;
	
	
	if(newMode){
		// SOMETHING ABOUT MODES HERE?
	}
	if(!isOn()){
		_frequency = newFreq;
		_mode = newMode;
 	}
	else if(force ||  (newFreq != _frequency) || newMode != _mode){
		
		printf("setFrequencyandMode(%s %u) %d \n", modeString(newMode).c_str(), newFreq, force);

		std::lock_guard<std::mutex> lock(_mutex);
 
		_frequency = newFreq;
		_mode = newMode;
		_mux =  MUX_MONO;

		// Intentionally tune at a higher frequency to avoid DC offset.
		double tuner_freq = newFreq + 0.25 * _sdr.getSampleRate();
		
		if(! _sdr.setFrequency(tuner_freq))
			return false;
		
		// create proper decoder
		if(_fmDecoder) {
			delete _fmDecoder;
			_fmDecoder = NULL;
		}
		if(_mode == VHF || _mode == GMRS) {
	 
			// changing FM frequencies means recreating the decoder
			
			// The baseband signal is empty above 100 kHz, so we can
			// downsample to ~ 200 kS/s without loss of information.
			// This will speed up later processing stages.
			unsigned int downsample = max(1, int(RtlSdr::default_sampleRate / 215.0e3));
			fprintf(stderr, "baseband downsampling factor %u\n", downsample);
			
			// Prevent aliasing at very low output sample rates.
			double bandwidth_pcm = min(FmDecoder::default_bandwidth_pcm,
												0.45 * _pcmrate);
			
			_fmDecoder = new FmDecoder(RtlSdr::default_sampleRate,
												newFreq - tuner_freq,
												_pcmrate,
												false,  // stereo
												FmDecoder::default_deemphasis,     // deemphasis,
												FmDecoder::default_bandwidth_if,   // bandwidth_if
												FmDecoder::default_freq_dev,       // freq_dev
												bandwidth_pcm,
												downsample
												);
		}
 		else if(_mode == BROADCAST_FM) {
	 
			// changing FM frequencies means recreating the decoder
			
			// The baseband signal is empty above 100 kHz, so we can
			// downsample to ~ 200 kS/s without loss of information.
			// This will speed up later processing stages.
			unsigned int downsample = max(1, int(RtlSdr::default_sampleRate / 215.0e3));
			fprintf(stderr, "baseband downsampling factor %u\n", downsample);
			
			// Prevent aliasing at very low output sample rates.
			double bandwidth_pcm = min(FmDecoder::default_bandwidth_pcm,
												0.45 * _pcmrate);
			
			_fmDecoder = new FmDecoder(RtlSdr::default_sampleRate,
												newFreq - tuner_freq,
												_pcmrate,
												true,  // stereo
												FmDecoder::default_deemphasis,     // deemphasis,
												FmDecoder::default_bandwidth_if,   // bandwidth_if
												FmDecoder::default_freq_dev,       // freq_dev
												bandwidth_pcm,
												downsample
												);
		}
		
		didUpdate = true;
		
		_sdr.resetBuffer();
		_output_buffer.flush();
		_shouldRead = true;
	}
	
	if(didUpdate){
		db->updateValue(VAL_MODULATION_MODE, _mode);
		db->updateValue(VAL_RADIO_FREQ, _frequency);
	//	db->updateValue(VAL_MODULATION_MUX, RadioMgr::MUX_UNKNOWN);
		display->showRadioChange();
	}
	
	return true;
}
 

uint32_t RadioMgr::frequency(){
	return _frequency;
 }

string RadioMgr::modeString(radio_mode_t mode){
 
	string str = "?";
	switch (mode) {
		case BROADCAST_AM:
			str = "AM";
			break;
			
		case BROADCAST_FM:
			str = "FM";
			break;
			
		case VHF:
			str = "VHF";
			break;
 
		case GMRS:
			str = "GMRS";
			break;
  
			
		default: ;
	}
 
	return str;
}


 RadioMgr::radio_mode_t RadioMgr::stringToMode(string str){
	 radio_mode_t mode = MODE_UNKNOWN;
	 
	 if(str == "AM") mode = BROADCAST_AM;
	 else  if(str == "FM") mode = BROADCAST_FM;
	 else if(str == "VHF") mode = VHF;
	 else if(str == "GMRS") mode = GMRS;
		return mode;
		
}


string RadioMgr::muxstring(radio_mux_t mux){
 
	string str ;
	switch (mux) {
		case MUX_STEREO:
			str = "ST";
			break;
			
		case MUX_QUAD:
			str = "QD";
			break;
			
		default:
			str = "       ";
	}
 
	return str;
}


string  RadioMgr::freqSuffixString(double hz){
	
	if(hz > 1710e3) { // Mhz
		return "Mhz";
	} else if(hz >= 1.0e3) {
		return "Khz";
	}
	
	return "";
}


bool RadioMgr::freqRangeOfMode(radio_mode_t mode, uint32_t & minFreq,  uint32_t &maxFreq){
	bool success = false;
	
	switch (mode) {
		case BROADCAST_AM:
			minFreq = 530e3;
			maxFreq = 1710e3;
			success = true;
			break;
			
		case BROADCAST_FM:
			minFreq = 87.9e6;
			maxFreq = 107.9e6;
			success = true;
  			break;
			 
		default: ;
	}
 
	return success;
}


uint32_t RadioMgr::nextFrequency(bool up,bool constrain){
	
	uint32_t newfreq = _frequency;
	
	if(constrain){
		PiCarMgr*		mgr 	= PiCarMgr::shared();
		PiCarMgr::station_info_t info;
		
		if(mgr->nextPresetStation(_mode, _frequency, up, info)){
			newfreq = info.frequency;
		}
	}else {
		switch (_mode) {
			case BROADCAST_AM:
				// AM steps are 10khz
				if(up) {
					newfreq+=10.e3;
				}
				else {
					newfreq-=10.e3;
				}
				newfreq = fmax(530e3, fmin(1710e3, newfreq));  //  pin freq
				break;
				
			case BROADCAST_FM:
				// AM steps are 200khz
				if(up) {
					newfreq+=200.e3;
				}
				else {
					newfreq-=200.e3;
				}
				newfreq = fmax(87.9e6, fmin(107.9e6, newfreq));  //  pin freq
				break;
				
			default:
				if(up) {
					newfreq+=1.e3;
				}
				else {
					newfreq-=1.e3;
				}
				break;
		}
	}
	return newfreq;
}

string  RadioMgr::hertz_to_string(double hz, int precision){
	
	char buffer[128] = {0};
 
	if(hz > 1710e3) { // Mhz
		sprintf(buffer, "%*.*f", precision+4, precision, hz/1.0e6);
	} else if(hz >= 1.0e3) {
		sprintf(buffer, "%4d", (int)round( hz/1.0e3));
	}
	
	return string(buffer);
}


// MARK: -  SDRReader thread

 
void RadioMgr::SDRReader(){
 
	IQSampleVector iqsamples;

	while(!_shouldQuit){
		{
			// radio is off sleep for awhile.
			if(!_isSetup || !_shouldRead){
 				usleep(2);
				continue;
			}
		}
		
		if (!_sdr.getSamples(iqsamples)) {
			//			 fprintf(stderr, "ERROR: getSamples\n");
			continue;
		}
		
		//		printf("read: %ld\n", iqsamples.size());
		
		_source_buffer.push(move(iqsamples));
	}
	
	_source_buffer.push_end();
}


void* RadioMgr::SDRReaderThread(void *context){
	RadioMgr* d = (RadioMgr*)context;

	//   the pthread_cleanup_push needs to be balanced with pthread_cleanup_pop
	pthread_cleanup_push(   &RadioMgr::SDRReaderThreadCleanup ,context);
 
	d->SDRReader();
	
	pthread_exit(NULL);
	
	pthread_cleanup_pop(0);
	return((void *)1);
}

 
void RadioMgr::SDRReaderThreadCleanup(void *context){
	//RadioMgr* d = (RadioMgr*)context;

 
//	printf("cleanup sdr\n");
}

// MARK: -  SDRProcessor  thread

 
/** Simple linear gain adjustment. */
void adjust_gain(SampleVector& samples, double gain)
{
	 for (size_t i = 0, n = samples.size(); i < n; i++) {
		  samples[i] *= gain;
	 }
}

void RadioMgr::SDRProcessor(){
	
//	DisplayMgr*		display 	= PiCarMgr::shared()->display();
//	PiCarDB*			db 		= PiCarMgr::shared()->db();

	bool inbuf_length_warning = false;
	SampleVector audiosamples;
	double audio_level = 0;
	bool got_stereo = false;
	
	for (unsigned int block = 0; !_shouldQuit;  block++) {
		
		if(!_isSetup){
			usleep(200000);
			continue;
		}

		// Check for overflow of source buffer.
		if (!inbuf_length_warning &&
			 _source_buffer.queued_samples() > 10 * RtlSdr::default_sampleRate) {
			fprintf(stderr,
					  "\nWARNING: Input buffer is growing (system too slow)\n");
			inbuf_length_warning = true;
		}
		
		// Pull next block from source buffer.
		IQSampleVector iqsamples = _source_buffer.pull();
		if (iqsamples.empty())
			continue;
 
		if((_mode == BROADCAST_FM  || _mode == VHF ||  _mode == GMRS)
			&& _fmDecoder != NULL){
			
			/// this block is critical.  dont change frequencies in the middle of a process.
			std::lock_guard<std::mutex> lock(_mutex);
			
			// Decode FM signal.
			_fmDecoder->process(iqsamples, audiosamples);
			
			// Measure audio level.
			double audio_mean, audio_rms;
			samples_mean_rms(audiosamples, audio_mean, audio_rms);
			audio_level = 0.95 * audio_level + 0.05 * audio_rms;
			
			// Set nominal audio volume.
			adjust_gain(audiosamples, 0.5);
			
			// Stereo indicator change
			bool detect = _fmDecoder->stereo_detected();
	 		if (detect != got_stereo) {
				got_stereo = detect;
 				_mux = detect? MUX_STEREO:MUX_MONO;
				
//				if (detect)
//					printf( "got stereo signal (pilot level = %f)\n",
//							 _fmDecoder->get_pilot_level());
//				else
//					printf( "lost stereo signal\n");
				
 //				display->showRadioChange();
			}
			
#if DEBUG_DEMOD
			
			// Show statistics.
//			fprintf(stderr, "\rblk=%6d  freq=%8.4fMHz  IF=%+5.1fdB  BB=%+5.1fdB  audio=%+5.1fdB ",
//					  block,
//					  _frequency *  1.0e-6,
//					  //					  (tuner_freq + _fmDecoder->get_tuning_offset()) * 1.0e-6,
//					  20*log10(_fmDecoder->get_if_level()),
//					  20*log10(_fmDecoder->get_baseband_level()) + 3.01,
//					  20*log10(audio_level) + 3.01);
			
			
//			// Show stereo status.
//	 			if (_fmDecoder->stereo_detected() != got_stereo) {
//
//		 		got_stereo = _fmDecoder->stereo_detected();
//				if (got_stereo)
//					fprintf(stderr, "\ngot stereo signal (pilot level = %f)\n",
//							  _fmDecoder->get_pilot_level());
//				else
//					fprintf(stderr, "\nlost stereo signal\n");
//			}
	
#endif
			// Throw away first block. It is noisy because IF filters
			// are still starting up.
			if (block > 0) {
				
				// Write samples to output.
				// Buffered write.
				_output_buffer.push(move(audiosamples));
			}
			
		}
		else {
			usleep(200000);
			continue;
		}
		
	}
	
}
 


void* RadioMgr::SDRProcessorThread(void *context){
	RadioMgr* d = (RadioMgr*)context;

	//   the pthread_cleanup_push needs to be balanced with pthread_cleanup_pop
	pthread_cleanup_push(   &RadioMgr::SDRProcessorThreadCleanup ,context);
 
	d->SDRProcessor();
	
	pthread_exit(NULL);
	
	pthread_cleanup_pop(0);
	return((void *)1);
}

 
void RadioMgr::SDRProcessorThreadCleanup(void *context){
	//RadioMgr* d = (RadioMgr*)context;

 
//	printf("cleanup sdr\n");
}
// MARK: -  Audio Output processor  thread

 
void RadioMgr::OutputProcessor(){
  
	AudioOutput*	 audio  = PiCarMgr::shared()->audio();

	while(!_shouldQuit){
		
		if(!_isSetup){
			usleep(200000);
			continue;
 		}
		
		if (_output_buffer.queued_samples() == 0) {
			 // The buffer is empty. Perhaps the output stream is consuming
			 // samples faster than we can produce them. Wait until the buffer
			 // is back at its nominal level to make sure this does not happen
			 // too often.
			
			
			// revisit this..  teh 2 is for stereo..
			
			_output_buffer.wait_buffer_fill(_pcmrate * 2);
			
			
		}
		// Get samples from buffer and write to output.
		SampleVector samples =_output_buffer.pull();
		audio->write(samples);
	}
	
 }


void* RadioMgr::OutputProcessorThread(void *context){
	RadioMgr* d = (RadioMgr*)context;

	//   the pthread_cleanup_push needs to be balanced with pthread_cleanup_pop
	pthread_cleanup_push(   &RadioMgr::OutputProcessorThreadCleanup ,context);
 
	d->OutputProcessor();
	
	pthread_exit(NULL);
	
	pthread_cleanup_pop(0);
	return((void *)1);
}

 
void RadioMgr::OutputProcessorThreadCleanup(void *context){
	//RadioMgr* d = (RadioMgr*)context;

 
//	printf("cleanup sdr\n");
}
