//
//  AirplayInput.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 10/13/22.
//

#pragma once

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
 
#include "ErrorMgr.hpp"
#include "CommonDefs.hpp"
#include "IQSample.h"

using namespace std;

 
class AirplayInput {
	 
public:
 
	static constexpr int 	default_blockLength = 4096;
	
	AirplayInput();
	~AirplayInput();
	
	bool begin(const char* path = "/tmp/shairport-sync-audio");
	bool begin(const char* path, int &error);
	
	void stop();
	bool isConnected();
 
	bool getSamples(SampleVector& audio);
 
	private:
 
	bool						_isSetup;
	unsigned int         _nchannels;
 
	int       				_blockLength;
 
 	bool  openAudioPipe(const char* audioPath,  int &error);
	void  closeAudioPipe();
	
	int	 	_fd;		// audio pipe fd
	
   };

