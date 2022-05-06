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
#include "RadioDataSource.hpp"
#include "AudioOutput.hpp"


int main(int argc, const char * argv[]) {
	
	string dev_display  = "/dev/ttyUSB0";
	string dev_audio  = "hw:CARD=wm8960soundcard,DEV=0";

	
	DisplayMgr*		display 	= DisplayMgr::shared();
	RadioMgr*		radio 	= RadioMgr::shared();
	AudioOutput* 	audio 	= AudioOutput::shared();
	constexpr int  pcmrate = 48000;

	TMP117 		tmp117;
	QwiicTwist	twist;
	
	RadioDataSource source(&tmp117, &twist);
	
	// annoying log messages in librtlsdr
	freopen( "/dev/null", "w", stderr );
	
	try {
		
		if(!display->begin(dev_display,B9600))
			throw Exception("failed to setup Display ");
		
		// these could fail.
		tmp117.begin(0x4A);
		twist.begin();
		
		display->setDataSource(&source);
		
		if(!display->setBrightness(7))
			throw Exception("failed to set brightness ");
		
		// find first RTS device
		auto devices = RtlSdr::get_devices();
		if( devices.size() == 0)
			throw Exception("No RTL devices found ");
		
		if(!audio->begin(dev_audio ,pcmrate, true ))
			throw Exception("failed to setup Audio ");
	
		audio->setVolume(.75);
		audio->setBalance(.1);
			
		if(!radio->begin(devices[0].index))
			throw Exception("failed to setup Radio ");
		
		display->showStartup();
 
		//	radio->setFrequency(1440e3);
		//	radio->setFrequency(88.1e6);
		radio->setFrequency(155.610e6);
		radio->setRadioMode(RadioMgr::RADIO_OFF);
		
		
		// dim button down
		twist.setColor(0, 8, 0);
		
		while(true){
			bool clicked = false;
			bool moved = false;
			
			if(twist.isMoved(moved) && moved){
				int16_t twistCount = 0;
				
				if(twist.getDiff(twistCount, true)) {
#if 0
					// controls channel
					auto newfreq = radio->nextFrequency(twistCount > 0);
					
					if(( radio->radioMode() != RadioMgr::RADIO_OFF)
						&& radio->setFrequency(newfreq)){
						display->showRadioChange();
					}
#else
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
#endif
				}
			}
			
			if(twist.isClicked(clicked) && clicked) {
				
				if(radio->radioMode() != RadioMgr::RADIO_OFF){
					radio->setRadioMode(RadioMgr::RADIO_OFF);
				}
				else {
					radio->setRadioMode(RadioMgr::BROADCAST_FM);
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
