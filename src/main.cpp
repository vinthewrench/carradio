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
#include "PiCanDB.hpp"
#include "PiCanMgr.hpp"


#if 1
int main(int argc, const char * argv[]) {

	PiCanMgr*		pican 	= PiCanMgr::shared();

	// annoying log messages in librtlsdr
//	freopen( "/dev/null", "w", stderr );
 
	if(!pican->begin()) {
		return 0;
	}
	
	// run the main loop.
	
	bool firstrun = true;
	while(true) {
		
		if(firstrun){
			sleep(5);
			
			pican->audio()->setVolume(.5);
			pican->radio()->setFrequencyandMode(RadioMgr::BROADCAST_FM, 101.900e6);
			firstrun = false;
			continue;
		}
	 
 		sleep(60);
	}

	return 0;

}

#else

int main(int argc, const char * argv[]) {
	
	string dev_display  = "/dev/ttyUSB0";
	//string dev_audio  = "hw:CARD=wm8960soundcard,DEV=0";
	string dev_audio  = "default";

	constexpr int  pcmrate = 48000;

	
	PiCanMgr*		pican 	= PiCanMgr::shared();

	DisplayMgr*		display 	= DisplayMgr::shared();
	RadioMgr*		radio 	= RadioMgr::shared();
	AudioOutput* 	audio 	= AudioOutput::shared();
	PiCanDB * 		db 		= PiCanDB ::shared();

	TMP117 		tmp117;
	QwiicTwist	twist;
	
	double savedFreq = 101.900e6;
	

	// annoying log messages in librtlsdr
//	freopen( "/dev/null", "w", stderr );
	
	try {
		
		if(!pican->begin())
			throw Exception("failed to setup PiCan System ");
		
		return 0;
		
//
//		if(!display->begin(dev_display,B9600))
//			throw Exception("failed to setup Display ");
//
		// these could fail.
		tmp117.begin(0x4A);
		twist.begin();
			
		if(!display->setBrightness(7))
			throw Exception("failed to set brightness ");
		
		// find first RTS device
		auto devices = RtlSdr::get_devices();
		if( devices.size() == 0)
			throw Exception("No RTL devices found ");
		
//		if(!audio->begin(dev_audio ,pcmrate, true ))
//			throw Exception("failed to setup Audio ");
//
//		audio->setVolume(.75);
//		audio->setBalance(.1);
			
		if(!radio->begin(devices[0].index, pcmrate))
			throw Exception("failed to setup Radio ");
		
		display->showStartup();
  
		radio->setFrequencyandMode(RadioMgr::BROADCAST_FM, savedFreq);
		
	//	radio->setFrequencyandMode(RadioMgr::RADIO_OFF);
		
 		// dim button down
		twist.setColor(0, 8, 0);
		
		while(true){
			bool clicked = false;
			bool moved = false;
			
			
			if(twist.isMoved(moved) && moved){
				int16_t twistCount = 0;
				
				if(twist.getDiff(twistCount, true)) {
#if 1
					// controls channel
					auto newfreq = radio->nextFrequency(twistCount > 0);
					auto mode = radio->radioMode();
					
					if(( mode != RadioMgr::RADIO_OFF)
		 				&& radio->setFrequencyandMode(mode, newfreq)){
						savedFreq = newfreq;
					}
#elif 0
					// controls volume
					auto volume = audio->volume();
					
					if(twistCount > 0) {
						
						if(volume < 1) {						// twist up
							volume +=.04;
							if(volume > 1) volume = 1.0;	// pin volume
							audio->setVolume(volume);
						}
					}
					else {
						if(volume > 0) {							// twist down
							volume -=.04;
							if(volume < 0) volume = 0.0;		// twist down
							audio->setVolume(volume);
						}
					}
					
					display->showVolumeChange();
#else
		
					// controls balance
					auto balance = audio->balance();
					
					if(twistCount > 0) {
						
						if(balance < 1) {						// twist up
							balance +=.04;
							if(balance > 1) balance = 1.0;	// pin volume
							audio->setBalance(balance);
						}
					}
					else {
						if(balance > -1) {							// twist down
							balance -=.04;
							if(balance < -1) balance = -1.;		// twist down
							audio->setBalance(balance);
						}
					}
					
					display->showBalanceChange();

#endif
				}
			}
			
			if(twist.isClicked(clicked) && clicked) {
				
				if(radio->radioMode() != RadioMgr::RADIO_OFF){
					radio->setFrequencyandMode(RadioMgr::RADIO_OFF);
				}
				else {
					radio->setFrequencyandMode(RadioMgr::BROADCAST_FM,savedFreq);
				}
				display->showRadioChange();
				
			}
			
			usleep(1);
		};
		
		
	}
	catch ( const Exception& e)  {
		printf("\tError %d %s\n\n", e.getErrorNumber(), e.what());
	}
	catch (std::invalid_argument& e)
	{
		printf("EXCEPTION: %s ",e.what() );
	}
	
	radio->stop();
	display->stop();
	
	return EXIT_SUCCESS;
}
#endif

