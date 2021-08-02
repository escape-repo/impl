#pragma once

#include "globals.hpp"

using namespace std;

#define QUEUE_SIZE		16
#define FRAME_SIZE		2048 // 1500 MTU
#define WORDS_PER_FRAME (FRAME_SIZE / WORD_SIZE)
#define FRM_BUF_SIZE	(QUEUE_SIZE * WORDS_PER_FRAME)
#define NULLNODE		-1

enum OpType
{
	OP_NONE,
	OP_ENQ,
	OP_DEQ,
	OP_EXT
};

struct Op
{
	OpType 	  type;
	FiveTuple fid;
};

class Queue
{
	public:
		Queue();
		
		short getNextFreeSpot();
		void copyHeader(Word& word);

		bool enqueue(Word& word, bool firstWord);
		bool dequeue(Word& word, bool firstWord);
		bool extract(FiveTuple& fid, Word& word, bool firstWord);

		Word frmBuf[FRM_BUF_SIZE];
		ap_uint<QUEUE_SIZE> bufBitmap; // Buffer availability
		Header hdrBuf[QUEUE_SIZE];
		short queue[QUEUE_SIZE]; // FIFO sequence of buffer locations
		
		uint32_t bufIndex;
		short pktIndex;
		short wordCounter;
		short size;
};

void queueHandler(
	stream<Word>& inData, // Enqueue
	stream<Op>&   op,
	stream<Word>& outDeq,
	stream<Word>& outExt);
