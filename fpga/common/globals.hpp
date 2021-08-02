#pragma once

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <math.h>
#include <stdint.h>
#include <cstdlib>

#include <hls_stream.h>
#include <ap_int.h>
#include <ap_axi_sdata.h>

using namespace hls;

#define WORD_SIZE 	   8 // Bytes per cycle
#define BYTES_PER_HDR 13 // 4 + 4 + 2 + 2 + 1

typedef ap_uint<8*BYTES_PER_HDR> Header;
typedef Header FiveTuple;

//typedef ap_axiu<8*WORD_SIZE, 1, 1, 1> Word;

struct Word {
	ap_uint<8*WORD_SIZE> data;
	ap_uint<1>	 		 last;
};
