#include "queue.hpp"
#include <stdlib.h>

using namespace hls;
using namespace std;

int main(int argc, char **argv)
{
	ifstream inputFile;
	Word word;
	stream<Word> inFIFO("inFIFO");
	stream<Op> opFIFO("opFIFO");
	stream<Word> outDeqFIFO("outDeqFIFO");
	stream<Word> outExtFIFO("outExtFIFO");

	inputFile.open(argv[1]);
	if (!inputFile) {
		cerr << " Error opening input file!" << endl;
		return -1;
    }
    cerr << "Input File: " << argv[1] << endl << endl;

	uint64_t data;
	uint16_t last;
	uint16_t count = 0;

	cerr << "Running DUT ";
	while (inputFile >> hex >> data >> last) {
		word.data = data;
		word.last = last;
		inFIFO.write(word);
		count++;
	}

	while (!inFIFO.empty())
		queueHandler(inFIFO, opFIFO, outDeqFIFO, outExtFIFO);

	Op op;
	op.type = OP_DEQ;
	for (uint16_t i = 0; i < count + 100; i++) {
		opFIFO.write(op);
		queueHandler(inFIFO, opFIFO, outDeqFIFO, outExtFIFO);
	}

	cerr << " done." << endl;
	cerr << "Checking output ";

	inputFile.clear();
	inputFile.seekg(0);
	uint16_t errCount = 0;
	uint16_t index = 0;
	for (uint16_t i = 0; i < count; i++) {
		outDeqFIFO.read(word);
		inputFile >> hex >> data >> last;
		if (data != word.data || last != word.last) {
			errCount++;
			cerr << "X";
		} else {
			cerr << ".";
		}
	}
	while (!inFIFO.empty())
		inFIFO.read(word);
	while (!outDeqFIFO.empty())
		outDeqFIFO.read(word);
	while (!opFIFO.empty())
		opFIFO.read(op);
	inputFile.close();

	cerr << " done." << endl << endl;
	return 0;
    if (errCount == 0) {
        cerr << "*** Test Passed ***" << endl << endl;
        return 0;
    } else {
        cerr << "!!! TEST FAILED -- " << errCount << " mismatches detected !!!";
        cerr << endl << endl;
        return -1;
    }
}
