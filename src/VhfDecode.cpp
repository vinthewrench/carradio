
#include <cassert>
#include <cmath>

#include "VhfDecode.hpp"

using namespace std;


#pragma clang diagnostic ignored "-Wconversion"


///** Fast approximation of atan function. */
//static inline Sample fast_atan(Sample x)
//{
//	 // http://stackoverflow.com/questions/7378187/approximating-inverse-trigonometric-funcions
//
//	 Sample y = 1;
//	 Sample p = 0;
//
//	 if (x < 0) {
//		  x = -x;
//		  y = -1;
//	 }
//
//	 if (x > 1) {
//		  p = y;
//		  y = -y;
//		  x = 1 / x;
//	 }
//
//	 const Sample b = 0.596227;
//	 y *= (b*x + x*x) / (1 + 2*b*x + x*x);
//
//	 return (y + p) * Sample(M_PI_2);
//}


/** Compute RMS level over a small prefix of the specified sample vector. */
static IQSample::value_type rms_level_approx(const IQSampleVector& samples)
{
	 unsigned int n = samples.size();
	 n = (n + 63) / 64;

	 IQSample::value_type level = 0;
	 for (unsigned int i = 0; i < n; i++) {
		  const IQSample& s = samples[i];
		  IQSample::value_type re = s.real(), im = s.imag();
		  level += re * re + im * im;
	 }

	 return sqrt(level / n);
}


/* ****************  class VhfDecoder  **************** */

VhfDecoder::VhfDecoder(double sample_rate_if,
							double tuning_offset,
							double sample_rate_pcm,
							double deemphasis,
							double bandwidth_if,
							double freq_dev,
							double bandwidth_pcm,
							unsigned int downsample,
							int squelch_level  )

	 // Initialize member fields
	 : m_sample_rate_if(sample_rate_if)
	 , m_sample_rate_baseband(sample_rate_if / downsample)
	 , m_tuning_table_size(64)
	 , m_tuning_shift(lrint(-64.0 * tuning_offset / sample_rate_if))
	 , m_freq_dev(freq_dev)
	 , m_downsample(downsample)
	 , m_if_level(0)
	 , m_baseband_mean(0)
	 , m_baseband_level(0)
	 , m_squelch_level(squelch_level)
	 ,m_is_squelched(false)

	 // Construct FineTuner
	 , m_finetuner(m_tuning_table_size, m_tuning_shift)

	 // Construct LowPassFilterFirIQ
	 , m_iffilter(10, bandwidth_if / sample_rate_if)

	 // Construct PhaseDiscriminator
	 , m_phasedisc(freq_dev / sample_rate_if)

	 // Construct DownsampleFilter for baseband
	 , m_resample_baseband(8 * downsample, 0.4 / downsample, downsample, true)

	 // Construct DownsampleFilter for mono channel
	 , m_resample_mono(
		  int(m_sample_rate_baseband / 1000.0),               // filter_order
		  bandwidth_pcm / m_sample_rate_baseband,             // cutoff
		  m_sample_rate_baseband / sample_rate_pcm,           // downsample
	  false)                                              // integer_factor

	 // Construct HighPassFilterIir
	 , m_dcblock_mono(30.0 / sample_rate_pcm)
	 , m_dcblock_stereo(30.0 / sample_rate_pcm)

	 // Construct LowPassFilterRC
	 , m_deemph_mono(
		  (deemphasis == 0) ? 1.0 : (deemphasis * sample_rate_pcm * 1.0e-6))
{
	 // nothing more to do
}


void VhfDecoder::process(const IQSampleVector& samples_in,
								SampleVector& audio)
{
	// Fine tuning.
	m_finetuner.process(samples_in, m_buf_iftuned);
	
	// Low pass filter to isolate station.
	m_iffilter.process(m_buf_iftuned, m_buf_iffiltered);
	
	// Measure IF level.
	double if_rms = rms_level_approx(m_buf_iffiltered);
	m_if_level = 0.95 * m_if_level + 0.05 * if_rms;
	
	// rms level is faster responding for triggering squelch
	int current_level  = int (20*log10(if_rms));
	
	if(m_squelch_level && ( current_level < m_squelch_level) ){
	
		m_is_squelched = true;
		printf("OFF rms: %.5f\t if: %.5f\t squelch: %3d <  %3d\n", if_rms, m_if_level, current_level ,m_squelch_level);

		for (unsigned int i = 0; i < m_buf_mono.size(); i++) {
			m_buf_mono[i] =  0.0;
		}
	}
	else
	{
		
		m_is_squelched = false;

		printf("ON rms: %.5f\t if: %.5f\t squelch: %3d <  %3d\n", if_rms, m_if_level, current_level ,m_squelch_level);

		// Extract carrier frequency.
		m_phasedisc.process(m_buf_iffiltered, m_buf_baseband);
		
		// Downsample baseband signal to reduce processing.
		if (m_downsample > 1) {
			SampleVector tmp(move(m_buf_baseband));
			m_resample_baseband.process(tmp, m_buf_baseband);
		}
		
		// Measure baseband level.
		double baseband_mean, baseband_rms;
		samples_mean_rms(m_buf_baseband, baseband_mean, baseband_rms);
		m_baseband_mean  = 0.95 * m_baseband_mean + 0.05 * baseband_mean;
		m_baseband_level = 0.95 * m_baseband_level + 0.05 * baseband_rms;
		
		// Extract mono audio signal.
		m_resample_mono.process(m_buf_baseband, m_buf_mono);
		
		//	 // DC blocking and de-emphasis.
		m_dcblock_mono.process_inplace(m_buf_mono);
		m_deemph_mono.process_inplace(m_buf_mono);
	}
	// Duplicate mono signal in left/right channels.
	mono_to_left_right(m_buf_mono, audio);
}


// Duplicate mono signal in left/right channels.
void VhfDecoder::mono_to_left_right(const SampleVector& samples_mono,
											  SampleVector& audio)
{
	 unsigned int n = samples_mono.size();

	 audio.resize(2*n);
	 for (unsigned int i = 0; i < n; i++) {
		  Sample m = samples_mono[i];
		  audio[2*i]   = m;
		  audio[2*i+1] = m;
	 }
}

