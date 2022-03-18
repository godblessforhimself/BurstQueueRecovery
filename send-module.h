/*
	The functions and variables used by send-main should be declared in send-module.h, and be defined in send-module.cpp.
*/
#ifndef SEND_MODULE_H
#define SEND_MODULE_H
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void innerMain(int argc, char *argv[]);
int parseArgs(int argc, char *argv[]);
void exchangeParameter();
void initialize();
void mainSend();
void clean();

#endif