//
//  RadioDataSource.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/5/22.
//

#pragma once

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>

#include "CommonDefs.hpp"
#include "DisplayMgr.hpp"

#include "TMP117.hpp"
#include "QwiicTwist.hpp"
#include "RadioMgr.hpp"


class RadioDataSource: public DisplayDataSource{
public:
	
	RadioDataSource(TMP117*, QwiicTwist* );
	//	virtual ~DisplayDataSource() {}
	//
	virtual bool getStringForKey(string_view key,  string &result);
	virtual bool getFloatForKey(string_view key,  float &result);
	virtual bool getDoubleForKey(string_view key,  double &result);
	virtual bool getIntForKey(string_view key,  int &result);
	
private:
	TMP117 		*_tmp117 = NULL;
	QwiicTwist	*_vol = NULL;
	
};
