#pragma once

#include "globals.hpp"

#define TABLE_SIZE 1024

enum OpType
{
	OP_NONE,
	OP_ADD,
	OP_GET
};

class FlowTable {
	public:
		FlowTable();

		bool flowExists(Header& hdr);

		bool addFlow(Header& hdr);
		bool getFlow(FiveTuple& fid);
	
		FiveTuple table[TABLE_SIZE];

		int nextWrite;
		int nextRead;
};

void flowTableHandler(
      stream<Header>& inData,
      stream<OpType>& op,
      stream<FiveTuple>& outData);
