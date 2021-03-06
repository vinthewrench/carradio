//
//  SDRDecoder.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 7/19/22.
//

#pragma once

#include <cstdint>
#include <vector>
#include "IQSample.h"

class SDRDecoder {
  
public:

	/** Destructor.
	 *  Virtual to allow for subclassing.
	 */
	virtual ~SDRDecoder() {};
	
 	virtual void process(const IQSampleVector& samples_in,
								SampleVector& audio) = 0;

};
