/*
 *  SoftFM - Software decoder for FM broadcast radio with RTL-SDR
 *
 *  Copyright (C) 2013, Joris van Rantwijk.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once
 
#include <cstdint>
#include <vector>
#include "Filter.hpp"
#include "SDRDecoder.hpp"
#include "FmDecode.hpp"



/** Complete decoder for FM broadcast signal. */
class VhfDecoder : public SDRDecoder
{
public:
	
	 static constexpr double default_deemphasis    =     5;
	 static constexpr double default_bandwidth_if  = 3000;
	 static constexpr double default_freq_dev      =  3000;
	 static constexpr double default_bandwidth_pcm =  2500;

	 /**
	  * Construct FM decoder.
	  *
	  * sample_rate_if   :: IQ sample rate in Hz.
	  * tuning_offset    :: Frequency offset in Hz of radio station with respect
	  *                     to receiver LO frequency (positive value means
	  *                     station is at higher frequency than LO).
	  * sample_rate_pcm  :: Audio sample rate.
	  *
	  * deemphasis       :: Time constant of de-emphasis filter in microseconds
	  *                     (50 us for broadcast FM, 0 to disable de-emphasis).
	  * bandwidth_if     :: Half bandwidth of IF signal in Hz
	  *                     (~ 100 kHz for broadcast FM)
	  * freq_dev         :: Full scale carrier frequency deviation
	  *                     (75 kHz for broadcast FM)
	  * bandwidth_pcm    :: Half bandwidth of audio signal in Hz
	  *                     (15 kHz for broadcast FM)
	  * downsample       :: Downsampling factor to apply after FM demodulation.
	  *                     Set to 1 to disable.
	  */
	VhfDecoder(double sample_rate_if,  //
				  double tuning_offset,   //
				  double sample_rate_pcm, //
				  double deemphasis=50,
				  double bandwidth_if=default_bandwidth_if,
				  double freq_dev=default_freq_dev,
				  double bandwidth_pcm=default_bandwidth_pcm,
				  unsigned int downsample=1,
				  int squelch_level  = 0);

	 /**
	  * Process IQ samples and return audio samples.
	  *
	  * If the decoder is set in stereo mode, samples for left and right
	  * channels are interleaved in the output vector (even if no stereo
	  * signal is detected). If the decoder is set in mono mode, the output
	  * vector only contains samples for one channel.
	  */
	 void process(const IQSampleVector& samples_in,
					  SampleVector& audio);


	 /** Return actual frequency offset in Hz with respect to receiver LO. */
	 double get_tuning_offset() const
	 {
		  double tuned = - m_tuning_shift * m_sample_rate_if /
							  double(m_tuning_table_size);
		  return tuned + m_baseband_mean * m_freq_dev;
	 }

	 /** Return RMS IF level (where full scale IQ signal is 1.0). */
	 double get_if_level() const
	 {
		  return m_if_level;
	 }

	 /** Return RMS baseband signal level (where nominal level is 0.707). */
	 double get_baseband_level() const
	 {
		  return m_baseband_level;
	 }

	void set_squelch_level(int level) 
	{
		m_squelch_level = level;
	}
		
	bool isSquelched() const  {
		return m_is_squelched;
	};
	
	void set_squelch_dwell(uint count){
		m_squelch_dwell = count;
	}

	

private:
 
	 /** Duplicate mono signal in left/right channels. */
	 void mono_to_left_right(const SampleVector& samples_mono,
									 SampleVector& audio);
 
	 // Data members.
	 const double    m_sample_rate_if;
	 const double    m_sample_rate_baseband;
	 const int       m_tuning_table_size;
	 const int       m_tuning_shift;
	 const double    m_freq_dev;
	 const unsigned int m_downsample;
	 double          m_if_level;
	 double          m_baseband_mean;
	 double          m_baseband_level;
	 int	           m_squelch_level;
	 uint 			  m_squelch_dwell;
	 bool      	     m_is_squelched;
 	 uint 			  m_signal_hits;

	 bool				  m_had_signal;
	 uint 			  m_squelch_hits;

	 IQSampleVector  m_buf_iftuned;
	 IQSampleVector  m_buf_iffiltered;
	 SampleVector    m_buf_baseband;
	 SampleVector    m_buf_mono;

	FineTuner           m_finetuner;
	LowPassFilterFirIQ  m_iffilter;
	PhaseDiscriminator  m_phasedisc;
	 DownsampleFilter    m_resample_baseband;
	 DownsampleFilter    m_resample_mono;
	 HighPassFilterIir   m_dcblock_mono;
	 HighPassFilterIir   m_dcblock_stereo;
	 LowPassFilterRC     m_deemph_mono;
};
