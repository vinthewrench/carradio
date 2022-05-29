//
//  BNO055_Compass.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/29/22.
//
 
/*
 	Bosch Sensortec MEMS BNO055 sensor
 https://github.com/BoschSensortec/BNO055_driver
*/
 
#pragma once

#include "I2C.hpp"
#include "bno055.h"

using namespace std;

class BNO055_Compass
{
 
public:
	BNO055_Compass();
	~BNO055_Compass();
 
	 
	bool begin(uint8_t deviceAddress = 0x28);
	bool begin(uint8_t deviceAddress,  int &error);
	void stop();
 
	bool reset();

	 
	uint8_t	getDevAddr();

	typedef struct {
		uint16_t 		sw_rev_id; /**< software revision id of bno055 */
		uint8_t 			accel_rev_id; /**< accel revision id of bno055 */
		uint8_t 			mag_rev_id; /**< mag revision id of bno055 */
		uint8_t 			gyro_rev_id; /**< gyro revision id of bno055 */
		uint8_t 			bl_rev_id; /**< boot loader revision id of bno055 */
	} BNO055_info_t;
	
	bool getInfo(BNO055_info_t&);
	
	typedef struct {
		float 		h; //Heading (Yaw)
		float 		p; //Pitch
		float 		r; //Roll
	} HRP_t;

 	bool getHRP(HRP_t&);
 
	
private:
 
	// C wrappers for bno055;
	static s8 BNO055_I2C_bus_write(void *context, u8 reg_addr, u8 *reg_data, u8 cnt);
	static s8 BNO055_I2C_bus_read(void *context, u8 reg_addr, u8 *reg_data, u8 cnt);
	
	
	I2C 		_i2cPort;
	bool		_isSetup;

	bno055_t _bno;

};
 
