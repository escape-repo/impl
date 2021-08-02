#include "queue.hpp"
#include <stdlib.h>

using namespace std;

Queue::Queue():
	bufIndex(0),
    pktIndex(0),
    wordCounter(0),
	size(0)
{
	for (int i = 0; i < FRM_BUF_SIZE; i++)
		frmBuf[i].data = frmBuf[i].last = 0;
	for (int i = 0; i < QUEUE_SIZE; i++) {
		bufBitmap.clear(i);
		queue[i] = NULLNODE;
		hdrBuf[i] = 0;
	}
}

short Queue::getNextFreeSpot()
{
#pragma HLS pipeline II=1
	Loop_getNextFreeSpot:for (short i = 0; i < QUEUE_SIZE; i++) {
#pragma HLS unroll
//#pragma HLS pipeline II=1
	 	if (!bufBitmap.test(i)) {
			bufBitmap.set(i);
			return i;
		}
	}
	return NULLNODE;
}

void Queue::copyHeader(Word& word)
{
    switch (wordCounter) {
        case 2:
                hdrBuf[pktIndex].range(7, 0) = word.data.range(7, 0);
                break;
        case 3:
                hdrBuf[pktIndex].range(55, 8) = word.data.range(47, 0);
                break;
        case 4:
                hdrBuf[pktIndex].range(103, 56) = word.data.range(63, 16);
                break;
    }
    wordCounter++;
}

bool Queue::enqueue(Word& word, bool firstWord)
{
#pragma HLS inline off
#pragma HLS pipeline II=1
	if (firstWord) {
		pktIndex = getNextFreeSpot();
		if (pktIndex == NULLNODE)
			return false;
		bufIndex = pktIndex * WORDS_PER_FRAME;
		wordCounter = 0;
		queue[size++] = pktIndex;
	}
	copyHeader(word);
    frmBuf[bufIndex++] = word;
    return true;
}

bool Queue::dequeue(Word& word, bool firstWord)
{
#pragma HLS inline off
#pragma HLS pipeline II=1
	if (firstWord) {
		if (size == 0) {
			word.last = 1;
			return false;
		}
		short head = queue[0];
		bufIndex = head * WORDS_PER_FRAME;
		bufBitmap.clear(head);
		size--;
		Loop_Deq_Shift:for (short i = 1; i < QUEUE_SIZE; i++) {
#pragma HLS unroll
//#pragma HLS pipeline II=1
			queue[i - 1] = queue[i];
		}
	}
	word = frmBuf[bufIndex++];
	return true;
}

bool Queue::extract(FiveTuple& fid, Word& word, bool firstWord)
{
#pragma HLS inline off
#pragma HLS pipeline II=4
	if (firstWord) {
		if (size == 0) {
			word.last = 1;
			return false;
		}
		short pktIndex = NULLNODE;
		Loop_Ext_Scan:for (short i = 0; i < QUEUE_SIZE; i++) {
#pragma HLS unroll
//#pragma HLS pipeline II=1
			if (hdrBuf[i] == fid) {
				pktIndex = i;
				break;
			}
		}
		if (pktIndex == NULLNODE) {
			word.last = 1;
			return false;
		}
		Loop_Ext_Shift:for (short i = 1; i < QUEUE_SIZE; i++) {
#pragma HLS unroll
//#pragma HLS pipeline II=1
			if (pktIndex < i)
				queue[i - 1] = queue[i];
		}
		bufIndex = pktIndex * WORDS_PER_FRAME;
		bufBitmap.clear(pktIndex);
		size--;
	}
	word = frmBuf[bufIndex++];
	return true;
}

static Queue q;

void queueHandler(
    stream<Word>& inData, // Enqueue
    stream<Op>&   op,
    stream<Word>& outDeq,
    stream<Word>& outExt)
{
#pragma HLS dataflow interval=1
#pragma HLS pipeline II=1

#pragma HLS interface port=inData axis
#pragma HLS interface port=op axis
#pragma HLS interface port=outDeq axis
#pragma HLS interface port=outExt axis

#pragma HLS resource variable=q.frmBuf core=RAM_2P_BRAM

#pragma HLS array_partition variable=q.hdrBuf dim=1 complete
#pragma HLS array_partition variable=q.queue dim=1 complete

	static enum State {IDLE, ENQ, DEQ, EXT} state;
	static Op currOp = {OP_NONE, 0};
	static bool first = true;
	Word currWord;

	switch (state) {
		case IDLE:
		{
			if (!op.empty()) {
				op.read(currOp);
				switch (currOp.type) {
					case OP_ENQ:
						state = ENQ;
						break;
					case OP_DEQ:
						state = DEQ;
						break;
					case OP_EXT:
						state = EXT;
						break;
					default:
						state = IDLE;
						break;
				}	
			}
			break;
		}
		case ENQ:
		{
			if (!inData.empty()) {
				inData.read(currWord);
				q.enqueue(currWord, first);
				if (first)
					first = false;
				if(currWord.last) {
					state = IDLE;
					first = true;
				}
			}
			break;
		}
		case DEQ:
		{
			q.dequeue(currWord, first);
			if (first)
				first = false;
			outDeq.write(currWord);
			if (currWord.last) {
				state = IDLE;
				first = true;
			}
			break;
		}
		case EXT:
		{
			q.extract(currOp.fid, currWord, first);
			if (first)
				first = false;
			outExt.write(currWord);
			if (currWord.last) {
				state = IDLE;
				first = true;
			}
			break;
		}
	}
}
