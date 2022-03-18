/*
	The functions and variables are declared in recv-module.h
	defined in recv-module.cpp
	organized and called in recv-main.cpp
*/
#ifndef RECV_MODULE_H
#define RECV_MODULE_H

#include "util.h"
#include <sys/socket.h>
#include <arpa/inet.h>
void innerMain(int argc, char *argv[]);
void parseParameter(int argc, char *argv[]);
void initialize();
void waitConnection();
void exchangeParameter();
void mainReceive();
void clean();
void openFile();
void closeFile();
void closeSocket();
void safeExit(int signal);
double *poolAlloc(int number);
int recvWithTimeout(int offset, int ps, int pn, double timeout, double *a, double *b);
#endif