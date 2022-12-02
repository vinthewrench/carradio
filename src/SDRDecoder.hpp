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

	virtual  double get_if_level()  const = 0;
	
	virtual double get_baseband_level() const = 0;
	
	virtual  bool 	canSquelch () const = 0;
	
	virtual  bool 	isSquelched() const = 0;
 
	virtual void 	set_squelch_level(int level)  = 0;
	
	virtual void 	set_squelch_dwell(uint count)  = 0;
 
};
