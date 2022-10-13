//
//  AirplayInput.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 10/13/22.
//

#include "AirplayInput.hpp"

#include <utility>      // std::pair, std::make_pair
#include <string>       // std::string
#include <queue>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <pthread.h>
#include <utility>      // std::pair, std::make_pair
#include <fcntl.h>
#include <sys/ioctl.h>


AirplayInput::AirplayInput(){
	
	_isSetup = false;
	_fd = -1;
	_blockLength = default_blockLength;
 
 }

AirplayInput::~AirplayInput(){
	stop();
}

bool AirplayInput::begin(const char* path){
	int error = 0;
	
	return begin(path, error);
}


bool AirplayInput::begin(const char* audioPath,  int &error){
 
	if(isConnected())
		return true;
	
	_isSetup = false;
	 
	if(openAudioPipe(audioPath, error)){
		_isSetup = true;
 	}
	
		
	return _isSetup;
}

void AirplayInput::stop(){
	
	if(_isSetup) {
		closeAudioPipe();
		_isSetup = false;
	}
}

bool  AirplayInput::isConnected() {
	bool val = false;
	
	val = _fd != -1;
	return val;
};


bool  AirplayInput::openAudioPipe(const char* audioPath,  int &error){
	
	if(!audioPath) {
		error = EINVAL;
		return false;
	}
	
	printf("openAudioPipe  %s\n", audioPath);
	
	int fd ;
	
	if((fd = ::open( audioPath, O_RDONLY  )) <0) {
		printf("Error %d, %s\n", errno, strerror(errno) );
		//	ELOG_ERROR(ErrorMgr::FAC_GPS, 0, errno, "OPEN %s", _ttyPath);
		error = errno;
		return false;
	}
	
	printf("Opened  %s\n", audioPath);
	
	_fd = fd;
	
	return true;
}


void AirplayInput::closeAudioPipe(){
	if(isConnected()){
		
		close(_fd);
		_fd = -1;
	}
}


static ssize_t safe_read(int fd, void *buf, size_t count)
{
	ssize_t result = 0, res;

	while (count > 0) {
		if ((res = read(fd, buf, count)) == 0)
			break;
		if (res < 0)
			return result > 0 ? result : res;
		count -= res;
		result += res;
		buf = (char *)buf + res;
	}
	return result;
}



bool AirplayInput::getSamples(SampleVector& audio){
	
	if(!_isSetup  )
		return  false;
	
	int nbytes = 0;
	if( ioctl(_fd, FIONREAD, &nbytes) < 0){
		printf("ioctl FIONREAD  %s \n",strerror(errno));
		return false;
	}
	
	if(nbytes > 0){
		
		printf("%d bytes avail \n",nbytes);
		
		int framesize = nbytes / 4;
		
		if (framesize > default_blockLength)
			framesize = default_blockLength;
		
		audio.resize(framesize);
		
		if( safe_read(_fd, audio.data(), framesize*4)  != framesize*4){
			printf("read fail  %s \n",strerror(errno));
			return false;
		}
		
		return true;
	}
	
	return  false;
	
}
