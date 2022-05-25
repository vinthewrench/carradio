//
//  FrameDB.cpp
//  canhacker
//
//  Created by Vincent Moscaritolo on 3/22/22.
//

#include "FrameDB.hpp"
 
static inline frameTag_t makeFrameTag(ifTag_t tag, canid_t canID){
	return  (( (uint64_t) tag) << 32) | canID;
};

static inline void  splitFrameTag(frameTag_t fTag, ifTag_t * ifTag, canid_t *canID){
	
	if(canID) *canID = fTag & 0xFFFFFFFF;
	if(ifTag) *ifTag = ((fTag >> 32) & 0xFF);
};


FrameDB::FrameDB(){
	_lastEtag = 0;
	_lastValueEtag = 0;
	_lastInterfaceTag = 0;
	_interfaces.clear();
	_schema.clear();
	_values.clear();
}

FrameDB::~FrameDB(){
	
}
 

bool FrameDB::registerProtocol(string ifName, CanProtocol *protocol) {

	// create the interface if it doesnt already exist?
	if(_interfaces.count(ifName) == 0){
		interfaceInfo_t ifInfo;
		ifInfo.ifName = ifName;
		ifInfo.protocols.clear();
		ifInfo.frames.clear();
		ifInfo.ifTag = _lastInterfaceTag++;
		_interfaces[ifName] = ifInfo;
	}

	// get the map entry for that interface.
	auto m1 = _interfaces.find(ifName);
	auto protoList = &m1->second.protocols;
	 
	// is it an already registered ?
	for(auto it = protoList->begin();  it != protoList->end(); ++it) {
		if(*it == protocol)
			return false;
	}
 
	protoList->push_back(protocol);	
	return true;
}

void FrameDB::unRegisterProtocol(string ifName, CanProtocol *protocol){
	
	// erase all interfaces?
	if(_interfaces.count(ifName)){
		_interfaces.erase(ifName);
		_values.clear();
		return;
	}
	
	// get the map entry for that interface.
	auto m1 = _interfaces.find(ifName);
	if(m1 == _interfaces.end())
		return;
	
	auto protoList = &m1->second.protocols;
	// erase all protocols?
	if(!protocol){
		protoList->clear();
		return;
	}
	
	// erase it if registered  ?
	for(auto it = protoList->begin();  it != protoList->end(); ++it) {
		if(*it == protocol) {
			protoList->erase(it);
			return;
		}
	}
	
}


vector<CanProtocol*>	FrameDB::protocolsForTag(frameTag_t tag){
	vector<CanProtocol*> protos;
	
 	ifTag_t	ifTag = 0;
	
	splitFrameTag(tag, &ifTag, NULL);

	for (const auto& [name ,_ ] : _interfaces){
		auto info = &_interfaces[name];
		if(info->ifTag	== ifTag){
			protos = info->protocols;
			break;
		}
	}

 	return protos;
}


FrameDB::valueSchema_t FrameDB::schemaForKey(string_view key){
	valueSchema_t schema = {"", "", UNKNOWN};
 
	if(_schema.count(key)){
		schema =  _schema[key];
	}
	
	return schema;
}

void FrameDB::addSchema(string_view key,  valueSchema_t schema){
	if( _schema.find(key) == _schema.end())
		_schema[key] = schema;
}
 


// MARK: -  FRAMES
 
int FrameDB::framesCount(){

	int count = 0;
	
	for (auto& [key, entry]  : _interfaces){
		count += entry.frames.size();
	}

	return count;
}


void FrameDB::clearFrames(string ifName){
	
	for (auto& [key, entry]  : _interfaces)
		for(auto proto : entry.protocols){
			proto->reset();
	}

	
	if(ifName.empty()){
		for (auto& [key, entry]  : _interfaces){
			entry.frames.clear();
		}
		
		clearValues();
	}
	else for (auto& [key, entry]  : _interfaces){
		if (strcasecmp(key.c_str(), ifName.c_str()) == 0){
			entry.frames.clear();
			return;
		}
	}
	_lastEtag = 0;
	_lastValueEtag = 0;
}

 
void  FrameDB::saveFrame(string ifName, can_frame_t frame, unsigned long  timeStamp){
	
	std::lock_guard<std::mutex> lock(_mutex);

	bitset<8> changed;
	bool isNew = false;
	long avgTime = 0;
	time_t now = time(NULL);
	
	_lastEtag++;

	canid_t can_id = frame.can_id & CAN_ERR_MASK;

	//Error check
	if(ifName.empty()) {
		throw Exception("ifName is blank");
	}

	// create the interface if it doesnt already exist?
	if(_interfaces.count(ifName) == 0){
		interfaceInfo_t ifInfo;
		ifInfo.ifName = ifName;
		ifInfo.protocols.clear();
		ifInfo.frames.clear();
		ifInfo.ifTag = _lastInterfaceTag++;
		_interfaces[ifName] = ifInfo;
	}
	
		// get the map entry for that interface.
	auto m1 = _interfaces.find(ifName);
	auto theFrames = &m1->second.frames;
	auto theProtocols = &m1->second.protocols;

	size_t count =  theFrames->count(can_id);
	if( count == 0){
		// create new frame entry
		frame_entry entry;
		entry.frame = frame;
		entry.timeStamp = timeStamp;
		entry.avgTime = 0;
		entry.eTag = _lastEtag;
		entry.updateTime = now;
		entry.lastChange.reset();
		theFrames->insert( std::pair<canid_t,frame_entry>(can_id,entry));
		isNew = true;
	}
	else {
		// can ID is already there
		auto e = theFrames->find(can_id);
		auto oldFrame = &e->second.frame;
		
		if(frame.can_dlc == oldFrame->can_dlc
			&& memcmp(frame.data, oldFrame->data, frame.can_dlc ) == 0){
			// frames are same - update timestamp and average
			long newAvg = ((timeStamp - e->second.timeStamp) + e->second.avgTime) / 2;
			e->second.timeStamp = timeStamp;
			e->second.avgTime = newAvg;
			avgTime =  newAvg;
		}
		else {
			// it changed
			//did the length change?
			if(frame.can_dlc != oldFrame->can_dlc) {
				for(int i = 0; i < frame.can_dlc; i++)
					changed.set(i);
			}
			else {
				for(int i = 0; i < frame.can_dlc; i++) {
					if(frame.data[i] != oldFrame->data[i])
						changed.set(i);
				}
			}
			
			// either way - update the frame
			// copy the frame
			memcpy( oldFrame,  &frame, sizeof(can_frame_t) );
			
			//- update timestamp and average
			long newAvg = ((timeStamp - e->second.timeStamp) + e->second.avgTime) / 2;
			e->second.timeStamp = timeStamp;
			e->second.avgTime = newAvg;
			e->second.eTag = _lastEtag;
			e->second.updateTime = now;
			e->second.lastChange = changed;
			avgTime =  newAvg;
		}
	}
	
	// tell the protocols something changed
 	if(isNew || (changed.count() > 0))
		for(auto proto : *theProtocols ){
			proto->processFrame(this, ifName, frame, now );
		};
	
}


vector<frameTag_t> FrameDB::framesUpdateSinceEtag(string ifName, eTag_t eTag, eTag_t *eTagOut ){
	
	std::lock_guard<std::mutex> lock(_mutex);
	vector<frameTag_t> tags = {};
	
	for (const auto& [name ,_ ] : _interfaces)
		if(ifName.empty() || ifName == name ) {
			auto info = &_interfaces[name];
			
			auto theFrames = &info->frames;
			
			for (const auto& [canid, frame] : *theFrames) {
				if(frame.eTag <= eTag){
					tags.push_back(makeFrameTag(info->ifTag, canid));
				}
			}
		}
	
	if(eTagOut)
		*eTagOut = _lastEtag;
	
	return tags;
}


vector<frameTag_t> 	FrameDB::allFrames(string ifName){
	vector<frameTag_t> tags = {};
	
	std::lock_guard<std::mutex> lock(_mutex);
	for (const auto& [name ,_ ] : _interfaces)
		if(ifName.empty() || ifName == name ) {
			auto info = &_interfaces[name];
			
			auto theFrames = &info->frames;
			
			for (const auto& [canid, frame] : *theFrames) {
				tags.push_back(makeFrameTag(info->ifTag, canid));
	 		}
		}
	
	return tags;

}

vector<frameTag_t>  	FrameDB::framesOlderthan(string ifName, time_t time){
	vector<frameTag_t> tags = {};
	
	std::lock_guard<std::mutex> lock(_mutex);

	
	for (const auto& [name ,_ ] : _interfaces)
		if(ifName.empty() || ifName == name ) {
			auto info = &_interfaces[name];
			
			auto theFrames = &info->frames;
			
			for (const auto& [canid, frame] : *theFrames) {
				if(frame.updateTime < time)
					tags.push_back(makeFrameTag(info->ifTag, canid));
			}
		}
	
	return tags;
}

bool FrameDB::frameWithTag(frameTag_t tag, frame_entry *frameOut, string *ifNameOut){
	
	std::lock_guard<std::mutex> lock(_mutex);
	
	frame_entry entry;
	
	canid_t 	can_id = 0;
	ifTag_t	ifTag = 0;
	
	splitFrameTag(tag, &ifTag, &can_id);
	
	for (const auto& [name ,_ ] : _interfaces) {
		auto info = &_interfaces[name];
		if(info->ifTag == ifTag){
			auto theFrames = &info->frames;
			if(theFrames->count(can_id) == 0 )
				return false;

			if(frameOut){
				auto e = theFrames->find(can_id);
				*frameOut =  e->second;
				
				if(ifNameOut)
					*ifNameOut = info->ifName;
				return true;
			}
 		}
	}
	
	return false;
}

// MARK: -   VALUES

void  FrameDB::clearValues(){
	_values.clear();
	_lastEtag = 0;
}

int FrameDB::valuesCount() {
	return (int) _values.size();
}


void FrameDB::updateValue(string_view key, string value, time_t when){
 
	if(when == 0)
		when = time(NULL);

	bool shouldUpdate = true;
	
	// filter out noise.
	if(_values.count(key)){
		auto lastValue = _values[key];
		
		if( lastValue.value == value)
			shouldUpdate = false;
	}
	
	if(shouldUpdate)
		_values[key] = {when, _lastValueEtag++, value};
}



vector<string_view> FrameDB::allValueKeys(){
	std::lock_guard<std::mutex> lock(_mutex);

	vector<string_view> keys;
	keys.clear();
	
	for (const auto& [key, value] : _values) {
			keys.push_back(key);
	}

	return keys;
}
  

vector<string_view> FrameDB::valuesUpdateSinceEtag(eTag_t eTag, eTag_t *eTagOut){
	
	std::lock_guard<std::mutex> lock(_mutex);
	vector<string_view> keys = {};
	
	for (const auto& [key, value] : _values) {
		if(value.eTag <= eTag)
			keys.push_back(key);
	}

	if(eTagOut)
		*eTagOut = _lastValueEtag;

	return keys;
};

vector<string_view> FrameDB::valuesOlderthan(time_t time){
	
	std::lock_guard<std::mutex> lock(_mutex);
	vector<string_view> keys = {};
	
	for (const auto& [key, value] : _values) {
		if(value.lastUpdate < time)
			keys.push_back(key);
	}

	return keys;
};


bool FrameDB::valueWithKey(string_view key, string *valueOut){
	std::lock_guard<std::mutex> lock(_mutex);
	
	if(_values.count(key) == 0 )
		return false;

	if(valueOut){
		*valueOut = _values[key].value;
	}
 	
	return true;
};
