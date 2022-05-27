//
//  RGB.hpp
//  carradio
//
//  Created by Vincent Moscaritolo on 5/26/22.
//
#pragma once
#include <stdlib.h>
#include <stdint.h>


struct RGB {
	union {
		struct {
			union {
				uint8_t r;
				uint8_t red;
			};
			union {
				uint8_t g;
				uint8_t green;
			};
			union {
				uint8_t b;
				uint8_t blue;
			};
		};
		uint8_t raw[3];
	};
	
	inline RGB() __attribute__((always_inline))
	{
	}

	inline RGB( uint8_t ir, uint8_t ig, uint8_t ib) 	__attribute__((always_inline))
	:r(ir), g(ig), b(ib)
	{};
	 
	inline RGB( uint32_t colorcode) __attribute__((always_inline))
	: r((colorcode >> 16) & 0xFF), g((colorcode >> 8) & 0xFF), b((colorcode >> 0) & 0xFF)
	
	{};

	inline RGB& operator= (const RGB& rhs)
	__attribute__((always_inline))
	 {
		  r = rhs.r;
		  g = rhs.g;
		  b = rhs.b;
		  return *this;
	 }
	
	 inline RGB& operator= (const uint32_t colorcode)
	__attribute__((always_inline))
	 {
		  r = (colorcode >> 16) & 0xFF;
		  g = (colorcode >>  8) & 0xFF;
		  b = (colorcode >>  0) & 0xFF;
		  return *this;
	 }
	};
		 
