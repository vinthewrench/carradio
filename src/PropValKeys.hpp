//
//  PropValKeys.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/9/22.
//
#pragma once


#include <strings.h>
#include <cstring>


#include "CommonDefs.hpp"

using namespace std;


// Well known keys

inline static const string VAL_CPU_INFO_TEMP 	= "CPU_TEMP";
inline static const string VAL_OUTSIDE_TEMP 		= "OUTSIDE_TEMP";

inline static const string VAL_AUDIO_VOLUME		= "vol";
inline static const string VAL_AUDIO_BALANCE		= "bal";
inline static const string VAL_RADIO_FREQ			= "freq";
inline static const string VAL_MODULATION_MODE	= "mode";
//inline static const string VAL_MODULATION_MUX	= "mux";
inline static const string VAL_RADIO_ON			= "radioON";


inline static const string PROP_CPU_TEMP_QUERY_DELAY 				= "cputemp-query-delay";
inline static const string  PROP_TEMPSENSOR_QUERY_DELAY				= "temp-query-delay";

inline static const string  PROP_LAST_RADIO_MODE				= "last_radio_mode";
inline static const string  PROP_LAST_RADIO_MODES				= "last_radio_modes";
inline static const string  PROP_LAST_RADIO_MODES_FREQ		= "freq";
inline static const string  PROP_LAST_RADIO_MODES_MODE		= "mode";
 
inline static const string  PROP_LAST_AUDIO_SETTING			= "audio_setting";
inline static const string  PROP_LAST_AUDIO_SETTING_VOL		= "vol";
inline static const string  PROP_LAST_AUDIO_SETTING_BAL		= "bal";

