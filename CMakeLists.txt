SET (CMAKE_C_COMPILER             "clang")
SET (CMAKE_CXX_COMPILER            "clang++")


cmake_minimum_required(VERSION 3.0)

project(carradio)

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

include_directories(/usr/include)
link_directories(/usr/lib)
target_link_libraries(carradio  PRIVATE Threads::Threads)

target_include_directories(carradio
	PRIVATE
	src
)

set(CMAKE_BINARY_DIR "bin")
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
