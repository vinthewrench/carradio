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
#include <unistd.h>

#include "CommonDefs.hpp"

#include "DisplayMgr.hpp"
#include "RadioMgr.hpp"
#include "TMP117.hpp"
#include "QwiicTwist.hpp"
#include "AudioOutput.hpp"
#include "PiCarDB.hpp"
#include "PiCarMgr.hpp"
#include "Utils.hpp"


int main(int argc, const char * argv[]) {
	
 	int childpid;
	if((childpid = fork()) == -1 )
	{
		perror("can't fork");
		exit(1);
	}
	else if(childpid == 0)
	{
		// launch shairport-sync
 		char *binaryPath = (char*) "/usr/local/bin/shairport-sync";
		char *args[] = {binaryPath, (char*)"--output=pipe", (char*)"-M", NULL};
		
		execv(binaryPath, args);
		exit(0);
	}
	else
	{
		
		PiCarMgr* pican 	= PiCarMgr::shared();
		
		// annoying log messages in librtlsdr
		freopen( "/dev/null", "w", stderr );
		
		if(!pican->begin()) {
			return 0;
		}
		
		// run the main loop.
		PRINT_CLASS_TID;
		
		bool firstrun = true;
		while(true) {
			
			if(firstrun){
				sleep(1);
#if defined(__APPLE__)
				
				// 			pican->stop();
				//
				pican->audio()->setVolume(.5);
				
				pican->radio()->setFrequencyandMode(RadioMgr::BROADCAST_FM, 101.900e6);
				//			pican->radio()->setFrequencyandMode(RadioMgr::VHF, 154455008);
				pican->radio()->setON(true);
				//			pican->saveRadioSettings();
				
				//			pican->radio()->setON(false);
				
				firstrun = false;
#endif
				continue;
			}
			
			sleep(1);
			
		}
		exit(0);
		
	}
	return 0;
 }

