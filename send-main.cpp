/*
	The main of sender
	using the variables and functions declared in send-module.h
*/
#include "send-module.h"
#include "util.h"
int main(int argc, char *argv[]) {
	tick();
	innerMain(argc, argv);
	tock();
}