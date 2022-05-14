//
//  main.cpp
//  vfdtest
//
//  Created by Vincent Moscaritolo on 4/13/22.
//


#include <stdio.h>
#include <stdlib.h>   // exit()

#include <stdexcept>
#include <sstream>
#include <ctime>
#include <cmath>
#include <iomanip>
#include <time.h>
#include <sys/time.h>
#include <iostream>
#include <filesystem> // C++17
#include <fstream>
#include "CommonDefs.hpp"

#include "DisplayMgr.hpp"
#include "RadioMgr.hpp"
#include "TMP117.hpp"
#include "QwiicTwist.hpp"
#include "AudioOutput.hpp"
#include "PiCarDB.hpp"
#include "PiCarMgr.hpp"

 
int main(int argc, const char * argv[]) {

	PiCarMgr* pican 	= PiCarMgr::shared();

	// annoying log messages in librtlsdr
 	freopen( "/dev/null", "w", stderr );
 
	if(!pican->begin()) {
		return 0;
	}
	
	// run the main loop.
	
	bool firstrun = true;
	while(true) {
		
		if(firstrun){
			sleep(5);
#if defined(__APPLE__)
			pican->audio()->setVolume(.5);
			pican->radio()->setFrequencyandMode(RadioMgr::BROADCAST_FM, 101.900e6);
			pican->radio()->setON(true);
			pican->saveRadioSettings();

			firstrun = false;
#endif
			continue;
		}
	 
 		sleep(60);
	}

	return 0;

}

