SET (CMAKE_C_COMPILER             "clang")
SET (CMAKE_CXX_COMPILER            "clang++")
set(CMAKE_CXX_FLAGS "-Wall -std=c++17 -O2 -ffast-math -ftree-vectorize ${EXTRA_FLAGS}")


cmake_minimum_required(VERSION 3.0)

if ( ${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR} )
    message( FATAL_ERROR "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there. You may need to remove CMakeCache.txt." )
endif()

project(carradio)

find_package(Threads)
find_package(PkgConfig)
find_package(ALSA REQUIRED)

# you will need to install the gpiod and the dev libraries before building
# sudo apt-get install gpiod libgpiod-dev
 
 #install the sqllite dev tools
# sudo apt-get install sqlite3
# sudo apt-get install libsqlite3-dev
 
# Find RTL-SDR library (for linking).
pkg_check_modules(PKG_RTLSDR librtlsdr)
find_path(RTLSDR_INCLUDE_DIR rtl-sdr.h
          HINT ${PKG_RTLSDR_INCLUDE_DIRS})
find_library(RTLSDR_LIBRARY librtlsdr.a
             HINT ${PKG_RTLSDR_LIBRARY_DIRS})

# Find libusb
pkg_check_modules(PKG_LIBUSB libusb-1.0)
find_path(LIBUSB_INCLUDE_DIR libusb.h
          HINT ${PKG_LIBUSB_INCLUDE_DIRS}
          PATH_SUFFIXES libusb-1.0)
find_library(LIBUSB_LIBRARY usb-1.0
             HINT ${PKG_LIBUSB_LIBRARY_DIRS})

if(RTLSDR_INCLUDE_DIR AND RTLSDR_LIBRARY)
    message(STATUS "Found librtlsdr: ${RTLSDR_INCLUDE_DIR}, ${RTLSDR_LIBRARY}")
else()
    message(WARNING "Can not find Osmocom RTL-SDR library")
    message("Try again with environment variable PKG_CONFIG_PATH")
    message("or with -DRTLSDR_INCLUDE_DIR=/path/rtlsdr/include")
    message("        -DRTLSDR_LIBRARY=/path/rtlsdr/lib/librtlsdr.a")
endif()

set(RTLSDR_INCLUDE_DIRS ${RTLSDR_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})
set(RTLSDR_LIBRARIES    ${RTLSDR_LIBRARY} ${LIBUSB_LIBRARY})


set(SOURCES
	src/main.cpp
	src/ErrorMgr.cpp
	src/I2C.cpp
	src/DuppaKnob.cpp
	src/DuppaEncoder.cpp
	src/DuppaLEDRing.cpp
	src/QwiicTwist.cpp
	src/VFD.cpp
	src/DisplayMgr.cpp
	src/RadioMgr.cpp
	src/FmDecode.cpp
	src/VhfDecode.cpp
	src/Filter.cpp
	src/AudioOutput.cpp
	src/AudioLineInput.cpp
	src/AirplayInput.cpp
	src/RtlSdr.cpp
	src/CPUInfo.cpp
	src/PiCarDB.cpp
	src/PiCarMgr.cpp
	src/TimeStamp.cpp
	src/GPSmgr.cpp
	src/tranmerc.cpp
	src/utm.cpp
	src/minmea.c
	src/CANBusMgr.cpp
	src/FrameDB.cpp
 	src/GMLAN.cpp
	src/OBD2.cpp
	src/Wranger2010.cpp
	src/PiCarCAN.cpp
	src/DTCcodes.cpp
	src/ArgononeFan.cpp
	src/W1Mgr.cpp
	src/dbuf.cpp
	src/DTCManager.cpp
#	src/MMC5983MA.cpp

# TMP117 Temp sensor
#	src/TempSensor.cpp
#	src/TMP117.cpp

#Bosch compass
#	src/bno055.c
#	src/BNO055_Compass.cpp
#	src/CompassSensor.cpp
)

 
add_executable(carradio ${SOURCES})


set_target_properties(carradio PROPERTIES
				CXX_STANDARD 17
				CXX_EXTENSIONS OFF
				)
 
set_target_properties(carradio PROPERTIES
			OUTPUT_NAME "carradio"
			)

			  
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(
    ${RTLSDR_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
    ${EXTRA_INCLUDES} )

link_directories(
	${RTLSDR_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
    ${EXTRA_INCLUDES}
    )

target_link_libraries(carradio
	 PRIVATE
	 ${CMAKE_THREAD_LIBS_INIT}
    ${RTLSDR_LIBRARIES}
    ${ALSA_LIBRARIES}
    ${EXTRA_LIBS}
 	 Threads::Threads
 	 gpiod
 	 sqlite3
 	 rt
 	)

target_include_directories(carradio
	PRIVATE
	src
)

set(CMAKE_BINARY_DIR "bin")
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

add_custom_target(copy_assets
     COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/Assets ${CMAKE_BINARY_DIR}
 )

add_dependencies(carradio copy_assets)
