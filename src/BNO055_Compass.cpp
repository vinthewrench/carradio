//
//  BNO055_Compass.cpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/29/22.
//
#include "BNO055_Compass.hpp"
#include "CommonDefs.hpp"

#include <unistd.h>
#include <errno.h>

#include "ErrorMgr.hpp"
 

static void BNO055_delay_msek(u32 msek)
{
	usleep(msek);
	
}

BNO055_Compass::BNO055_Compass(){
	_isSetup = false;
	
	_bno.context = (void*) this;
	_bno.bus_read = BNO055_I2C_bus_read;
	_bno.bus_write = BNO055_I2C_bus_write;
	_bno.delay_msec = BNO055_delay_msek;
}

BNO055_Compass::~BNO055_Compass(){
	stop();
}

bool BNO055_Compass::begin(uint8_t deviceAddress){
	int error = 0;

	return begin(deviceAddress, error);
}
 
bool BNO055_Compass::begin(uint8_t deviceAddress,   int &error){
	
	constexpr u8  BNO055_CHIPID  = 0xa0;
	
	
	if(1 /*  _i2cPort.begin(deviceAddress, error) */ ){
		s8 ret = bno055_init(&_bno)
		&& (_bno.chip_id == BNO055_CHIPID);
		
		if(ret == BNO055_SUCCESS){
			
			// set the device into  compass mode
			ret = bno055_set_operation_mode(BNO055_OPERATION_MODE_COMPASS);
			if(ret == BNO055_SUCCESS){
				
				//		delay(1);
				
				_isSetup = true;
				
			}
			else
				error = ENODEV;
			
		}
		else
			error = ENXIO;
		
	}
	
	return _isSetup;
}
 
void BNO055_Compass::stop(){
	_isSetup = false;
	_i2cPort.stop();

	//	LOG_INFO("TMP117(%02x) stop\n",  _i2cPort.getDevAddr());
}
 
uint8_t	BNO055_Compass::getDevAddr(){
	return _i2cPort.getDevAddr();
};

// MARK: -   wrappers for bno055;

 
s8 BNO055_Compass::BNO055_I2C_bus_write(void *context, u8 reg_addr, u8 *reg_data, u8 cnt){
	
	BNO055_Compass* d = (BNO055_Compass*)context;
	bool success = false;

	printf("BNO055 write (%02x, %d) \n", reg_addr, cnt);

	if(cnt == 1)
		success = d->_i2cPort.writeByte(reg_addr, reg_data[0]);
  	else
		success = d->_i2cPort.writeBlock(reg_addr, cnt, reg_data);
  
	return success?BNO055_SUCCESS:BNO055_ERROR;
  };

 
s8 BNO055_Compass::BNO055_I2C_bus_read(void *context, u8 reg_addr, u8 *reg_data, u8 cnt)
{
	
	BNO055_Compass* d = (BNO055_Compass*)context;
	bool success = false;
	
	printf("BNO055 read  (%02x, %d) \n", reg_addr, cnt);
	
	if(cnt == 1)
 		success = d->_i2cPort.readByte(reg_addr, reg_data);
	else
		success =  d->_i2cPort.readBlock(reg_addr, cnt, reg_data);
	
	return success?BNO055_SUCCESS:BNO055_ERROR;
};


// MARK: -


bool BNO055_Compass::getInfo(BNO055_Compass::BNO055_info_t& info){
	bool success = false;

	if(_isSetup){
		info.sw_rev_id 			= _bno.sw_rev_id;
 		info.accel_rev_id 		= _bno.accel_rev_id;
		info.mag_rev_id 			= _bno.mag_rev_id;
		info.gyro_rev_id 			= _bno.gyro_rev_id;
		info.bl_rev_id 			= _bno.bl_rev_id;
		success = true;
 	}
	
	return success;
}



bool BNO055_Compass::getHRP(HRP_t&  hrp){
	bool success = false;

	if(_isSetup){
		
		struct bno055_euler_t eul;
		
		if( bno055_read_euler_hrp(&eul)== BNO055_SUCCESS){
			hrp.h = float(eul.h) / 16.0 ;
			hrp.r = float(eul.r) / 16.0 ;
			hrp.p = float(eul.p) / 16.0 ;
			success = true;
		}
 
	}
	
	return success;

}


/* to save settings,.,
 
//  Accelerometer Offset registers/
 #define BNO055_ACCEL_OFFSET_X_LSB_ADDR      (0X55)
 #define BNO055_ACCEL_OFFSET_X_MSB_ADDR      (0X56)
 #define BNO055_ACCEL_OFFSET_Y_LSB_ADDR      (0X57)
 #define BNO055_ACCEL_OFFSET_Y_MSB_ADDR      (0X58)
 #define BNO055_ACCEL_OFFSET_Z_LSB_ADDR      (0X59)
 #define BNO055_ACCEL_OFFSET_Z_MSB_ADDR      (0X5A)

//  Magnetometer Offset registers/
 #define BNO055_MAG_OFFSET_X_LSB_ADDR        (0X5B)
 #define BNO055_MAG_OFFSET_X_MSB_ADDR        (0X5C)
 #define BNO055_MAG_OFFSET_Y_LSB_ADDR        (0X5D)
 #define BNO055_MAG_OFFSET_Y_MSB_ADDR        (0X5E)
 #define BNO055_MAG_OFFSET_Z_LSB_ADDR        (0X5F)
 #define BNO055_MAG_OFFSET_Z_MSB_ADDR        (0X60)

//  Gyroscope Offset registers
 #define BNO055_GYRO_OFFSET_X_LSB_ADDR       (0X61)
 #define BNO055_GYRO_OFFSET_X_MSB_ADDR       (0X62)
 #define BNO055_GYRO_OFFSET_Y_LSB_ADDR       (0X63)
 #define BNO055_GYRO_OFFSET_Y_MSB_ADDR       (0X64)
 #define BNO055_GYRO_OFFSET_Z_LSB_ADDR       (0X65)
 #define BNO055_GYRO_OFFSET_Z_MSB_ADDR       (0X66)

//  Radius registers/
 #define BNO055_ACCEL_RADIUS_LSB_ADDR        (0X67)
 #define BNO055_ACCEL_RADIUS_MSB_ADDR        (0X68)
 #define BNO055_MAG_RADIUS_LSB_ADDR          (0X69)
 #define BNO055_MAG_RADIUS_MSB_ADDR          (0X6A)


 claibrartion is at
 
 
 https://github.com/zischknall/BohleBots_BNO055/blob/master/src/BohleBots_BNO055.cpp
 
 BNO055_CALIB_STAT_ADDR
 bool BNO::isCalibrated()	//Gets the latest calibration values and does a bitwise and to return a true if everything is fully calibrated
 {
	 getCalibStat(&_calibData);
	 if((_calibData.sys & _calibData.gyr & _calibData.acc & _calibData.mag) == 3) return true;
	 return false;
 }


 void BNO::getCalibStat(struct calibStat *ptr)	//gets current calibration status
 {
	 uint8_t tmp = readRegister(CALIB_STAT_ADDR);
	 ptr->sys = (tmp&B11000000)>>6;
	 ptr->gyr = (tmp&B00110000)>>4;
	 ptr->acc = (tmp&B00001100)>>2;
	 ptr->mag = tmp&B00000011;
 }
*/
