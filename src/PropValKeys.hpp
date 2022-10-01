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
inline static const string VAL_FAN_SPEED			= "FAN_SPEED";

inline static const string VAL_OUTSIDE_TEMP 		= "OUTSIDE_TEMP";
inline static const string VAL_INSIDE_TEMP 		= "INSIDE_TEMP";
inline static const string VAL_COMPASS_BEARING 		= "COMPASS_BEARING";
inline static const string VAL_COMPASS_TEMP 		= "COMPASS_TEMP";

inline static const string VAL_AUDIO_VOLUME		= "vol";
inline static const string VAL_AUDIO_BALANCE		= "bal";
inline static const string VAL_RADIO_FREQ			= "freq";
inline static const string VAL_MODULATION_MODE	= "mode";
inline static const string VAL_RADIO_ON			= "radioON";
inline static const string VAL_AUTO					= "auto";


// json data
 
inline static const string  PROP_LAST_WRITE_DATE				= "last_write";

inline static const string PROP_CPU_TEMP_QUERY_DELAY 			= "cputemp-query-delay";
inline static const string PROP_FAN_QUERY_DELAY 				= "fan-query-delay";

inline static const string  PROP_TEMPSENSOR_QUERY_DELAY		= "temp-query-delay";
inline static const string  PROP_COMPASS_QUERY_DELAY			= "compass-query-delay";

inline static const string  PROP_LAST_RADIO_MODE				= "last_radio_mode";
inline static const string  PROP_LAST_RADIO_MODES				= "last_radio_modes";
inline static const string  PROP_LAST_RADIO_MODES_FREQ		= "freq";
inline static const string  PROP_LAST_RADIO_MODES_MODE		= "mode";
 
inline static const string  PROP_LAST_AUDIO_SETTING			= "audio_setting";
inline static const string  PROP_LAST_AUDIO_SETTING_VOL		= "vol";
inline static const string  PROP_LAST_AUDIO_SETTING_BAL		= "bal";
inline static const string  PROP_LAST_AUDIO_SETTING_FADER	= "fade";

inline static const string PROP_AUTO_DIMMER_MODE				= "auto_dimmer_mode";
inline static const string PROP_DIMMER_LEVEL						= "dimmer_level";

inline static const string PROP_AUTO_SHUTDOWN_MODE				= "auto_shutdown_mode";
inline static const string PROP_SHUTDOWN_DELAY					= "shutdown_delay";

inline static const string  PROP_CANBUS_DISPLAY				= "canbus-display";
inline static const string  PROP_LINE							= "line";
inline static const string  PROP_TITLE							= "title";
inline static const string  PROP_KEY							= "key";
inline static const string  PROP_ID								= "id";

inline static const string  PROP_UUID							= "uuid";
inline static const string  PROP_LATITUDE						= "latitude";
inline static const string  PROP_LONGITUDE					= "longitude";
inline static const string  PROP_ALTITUDE						= "altitude";
inline static const string  PROP_DOP							= "dop";  //  dilution of precision
inline static const string  PROP_TIMESTAMP					= "timestamp";   

inline static const string  PROP_WAYPOINTS					= "waypoints";

inline static const string  PROP_LAST_MENU_SELECTED		= "last_menu";

inline static const string  PROP_TUNER_MODE					= "tuner_knob_mode";
inline static const string  PROP_PRESETS						= "tuner_presets";
inline static const string  SCANNER_FREQS						= "scanner_freqs";

inline static const string  PROP_PRESET_FREQ					= "freq";
inline static const string  PROP_PRESET_MODE					= "mode";
inline static const string  PROP_SYNC_CLOCK_TO_GPS			= "clocksync_gps_secs";
inline static const string  PROP_W1_MAP						= "w1Map";
inline static const string  PROP_SQUELCH_LEVEL				= "squelch";

