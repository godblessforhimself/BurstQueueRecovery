/*
	The functions and variables which are used by both sender and receiver should be declared in util.h
	The implements should be in util.cpp
*/
#ifndef UTIL_H
#define UTIL_H
#include <time.h>
#include <stdarg.h>
#include <signal.h>
#include <assert.h>
#include <netinet/tcp.h> // TCP_MAXSEG
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> //close
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm> //sort
#include <iostream>
#include <getopt.h>
using namespace std;
#define CONTROL_MESSAGE_LENGTH_1 (100)
#define CONTROL_MESSAGE_LENGTH_2 (10)
#define READY_FOR_RECEIVE (77017)
#define MaxBufferLength (10000)
#define DoubleMax (1e6)
#define DoubleMin (0)
#define DurationMax (24000 * 1e-6)
enum ctrlSignal {
	error,
	preheat, /* send 10 packet to preheat */
	preheatTimeout, /* wait more than 1 second */
	firstTrain, /* send first train immediately */
	nextTrain, /* update duration, wait trainGap, send next train  */
	nextStream, /* update duration, wait streamGap, send next stream */
	retransmit, /* train id-=1, wait trainGap, resend train */
	end, /* all streams has received */
};
struct timestampPacket {
	int packetId;
	double timestamp[2];
	void host2network();
	void network2host();
};
struct signalPacket{
	int signal;
	int loadNumber;
	double abw;
	double inspectGap;
	int specialFlag;
	void host2network();
	void network2host();
};
struct controlParameter {
	int param[8];
	void host2network();
	void network2host();
};
extern char udpBuffer[MaxBufferLength];
extern clockid_t clockToUse;
extern timespec programBegin, programEnd;
extern timestampPacket *tpBuffer;
int setRecvBuf(int fd, int size);
int setReuseAddr(int fd);
void setSockaddr(sockaddr_in *addr, int port);
void setSockaddr(sockaddr_in *addr, char *ipStr, int port);
void setBusyPoll(int fd, int busyPoll);
int bindAddress(int fd, sockaddr_in *addr);
int getMss(int connFd);
double timespec2double(timespec t);
double getTimeDouble();
timespec double2timespec(double t);
void printTimespec(timespec t);
void setTimestampPacket(int id, double t1, double t2);
void sendSignal(int fd, signalPacket *sp);
int waitSignal(int fd, signalPacket *sp);
double getRate(int byte, double t);
void tick();
void tock();
#endif