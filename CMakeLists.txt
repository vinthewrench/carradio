SET (CMAKE_C_COMPILER             "clang")
SET (CMAKE_CXX_COMPILER            "clang++")
set(CMAKE_CXX_FLAGS "-Wall -std=c++11 -O2 -ffast-math -ftree-vectorize ${EXTRA_FLAGS}")

cmake_minimum_required(VERSION 3.0)

project(carradio)

find_package(Threads)
find_package(PkgConfig)
find_package(ALSA REQUIRED)


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
	src/TMP117.cpp
#	src/INA219.cpp
	src/QwiicTwist.cpp
	src/VFD.cpp
	src/DisplayMgr.cpp
	src/RadioMgr.cpp
	src/RadioDataSource.cpp
	src/AudioOutput.cpp
	src/RtlSdr.cpp
#	src/MicroNMEA.cpp
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
 	)

target_include_directories(carradio
	PRIVATE
	src
)

set(CMAKE_BINARY_DIR "bin")
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
