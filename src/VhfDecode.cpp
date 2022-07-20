//
//  VhfDecode.c
//  carradio
//
//  Created by Vincent Moscaritolo on 7/19/22.
//

#include "VhfDecode.hpp"



VhfDecode::~VhfDecode(){
	
}


VhfDecode::VhfDecode(){
	
	
}


/**
 * Process IQ samples and return audio samples.
 *
 * If the decoder is set in stereo mode, samples for left and right
 * channels are interleaved in the output vector (even if no stereo
 * signal is detected). If the decoder is set in mono mode, the output
 * vector only contains samples for one channel.
 */
void VhfDecode::process(const IQSampleVector& samples_in,
								SampleVector& audio){
	
}

