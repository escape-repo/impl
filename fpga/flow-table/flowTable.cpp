#include "flowTable.hpp"
#include <stdlib.h>

using namespace std;

FlowTable::FlowTable():
	nextWrite(0),
	nextRead(0)
{
	for (int i = 0; i < TABLE_SIZE; i++)
		table[i] = 0;
}

bool FlowTable::flowExists(Header& hdr)
{
    Loop_flowExists:for (int i = 0; i < TABLE_SIZE; i++) {
#pragma HLS pipeline II=1
		if (table[i] == hdr)
			return true;
	}
	return false;
}

bool FlowTable::addFlow(Header& hdr)
{
#pragma HLS inline off
#pragma HLS pipeline II=1
	if (flowExists(hdr))
		return false;
	table[nextWrite] = hdr;
	nextWrite = (nextWrite + 1) % TABLE_SIZE;
	return true;
}

bool FlowTable::getFlow(FiveTuple& fid)
{
#pragma HLS inline off
#pragma HLS pipeline II=1
	if (table[nextRead] == 0) /* No more flows */
		return false;
	fid = table[nextRead];
	nextRead = (nextRead + 1) % TABLE_SIZE;
	return true;
}

static FlowTable ft;

void flowTableHandler(
      stream<Header>& inData,
      stream<OpType>& op,
      stream<FiveTuple>& outData)
{
#pragma HLS dataflow interval=1
#pragma HLS pipeline II=1

#pragma HLS interface port=inData axis
#pragma HLS interface port=op axis
#pragma HLS interface port=outData axis

#pragma HLS array_partition variable=ft.table dim=1 complete

	static enum State {IDLE, ADD, GET} state;
    static OpType currOp = OP_NONE;
    Header currHdr;

	switch (state) {
		case IDLE:
        {
            if (!op.empty()) {
                op.read(currOp);
                switch (currOp) {
                    case OP_ADD:
                        state = ADD;
                        break;
                    case OP_GET:
                        state = GET;
                        break;
                    default:
                        state = IDLE;
                        break;
                }
            }
            break;
        }
		case ADD:
        {
            if (!inData.empty()) {
                inData.read(currHdr);
                ft.addFlow(currWord);
                state = IDLE;
            }
            break;
        }
		case GET:
        {
			FiveTuple fid;
			ft.getFlow(fid);
			outData.write(fid);
			state = IDLE;
            break;
        }
	}
}
