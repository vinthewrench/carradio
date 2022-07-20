//
//  VhfDecode.h
//  carradio
//
//  Created by Vincent Moscaritolo on 7/19/22.
//

#pragma once
 
#include <cstdint>
#include <vector>
#include "Filter.hpp"
#include "SDRDecoder.hpp"
 

/** Complete decoder for FM broadcast signal. */
class VhfDecode : public SDRDecoder
{
public:
	
	~VhfDecode();

	VhfDecode();
	
	
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



};

