/*
	calling functions from recv-module.h and util.h
*/
#include "recv-module.h"
#include "util.h"

int main(int argc, char *argv[]) {
	tick();
	innerMain(argc, argv);
	tock();
}