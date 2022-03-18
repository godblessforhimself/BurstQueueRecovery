#include "util.h"
clockid_t clockToUse = CLOCK_MONOTONIC;
timespec programBegin, programEnd;
timespec utilTimeTmp;
char udpBuffer[MaxBufferLength];
timestampPacket *tpBuffer = (timestampPacket*)udpBuffer;
void signalPacket::network2host(){
	signal = ntohl(signal);
	loadNumber = ntohl(loadNumber);
	specialFlag = ntohl(specialFlag);
}
void signalPacket::host2network(){
	signal = htonl(signal);
	loadNumber = htonl(loadNumber);
	specialFlag = htonl(specialFlag);
}
void controlParameter::network2host() {
	size_t len = sizeof(param)/sizeof(param[0]);
	for (int i = 0; (size_t)i < len; i++) {
		param[i] = ntohl(param[i]);
	}
}
void controlParameter::host2network(){
	size_t len = sizeof(param)/sizeof(param[0]);
	for (int i = 0; (size_t)i < len; i++) {
		param[i] = htonl(param[i]);
	}
}
void timestampPacket::network2host() {
	/* 
		TODO: double decode
		google protocol buffers
	*/
	packetId = ntohl(packetId);
}
void timestampPacket::host2network() {
	/* 
		TODO: double encode
		google protocol buffers
	*/
	packetId = htonl(packetId);
}
int setRecvBuf(int fd, int size) {
	socklen_t optlen = sizeof(size);
	return setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, optlen);
}
int setReuseAddr(int fd) {
    int optval = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int)) == -1) {
        return -1;
    }
    return 0;
}
void setBusyPoll(int fd, int busy_poll) {
	int r = setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, (char *)&busy_poll, sizeof(busy_poll));
	if (r < 0) {
		perror("setsockopt(SO_BUSY_POLL)");
	}
}
void setSockaddr(sockaddr_in *addr, int port) {
	/* set IP of addr to INADDR_ANY and port to param port */
    memset(addr, 0, sizeof(sockaddr_in));
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = htonl(INADDR_ANY);
    addr->sin_port = htons(port);
}
int bindAddress(int fd, sockaddr_in *addr) {
	/* bind listen socket to address */
    if (bind(fd, (sockaddr*)addr, sizeof(sockaddr_in))) {
        return -1;
    }
    return 0;
}
int getMss(int conn_fd){
	int opt_len, mss;
	opt_len = sizeof(mss);
	if (getsockopt(conn_fd, IPPROTO_TCP, TCP_MAXSEG, (char*)&mss, (socklen_t*)&opt_len)){
		return -1;
	} else {
		return mss;
	}
}
void setSockaddr(sockaddr_in *addr, char *ip_str, int port) {
	/* set address to (IP,port) */
	addr->sin_addr.s_addr = inet_addr(ip_str);
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
}
double timespec2double(timespec t){
	return t.tv_sec + (double)t.tv_nsec / (1e9);
}
double getTimeDouble() {
	clock_gettime(clockToUse, &utilTimeTmp);
	return utilTimeTmp.tv_sec + (double)utilTimeTmp.tv_nsec / (1e9);
}
timespec double2timespec(double t){
	timespec ret;
	ret.tv_sec = (int)t;
	ret.tv_nsec = (t - ret.tv_sec) * 1e9;
	return ret;
}
void printTimespec(timespec t){
	printf("%10lu %09lu\n", t.tv_sec, t.tv_nsec);
}
void sendSignal(int fd, signalPacket *sp) {
	/* sp 未做网络本地转换 */
	ssize_t ret;
	sp->host2network();
	memcpy(udpBuffer, sp, sizeof(signalPacket));
	do {
		ret = send(fd, udpBuffer, CONTROL_MESSAGE_LENGTH_1, 0);
	} while (ret != CONTROL_MESSAGE_LENGTH_1);
}
int waitSignal(int fd, signalPacket *sp) {
	ssize_t ret;
	do {
		ret = recv(fd, udpBuffer, CONTROL_MESSAGE_LENGTH_1, 0);
	} while (ret != CONTROL_MESSAGE_LENGTH_1);
	if (sp) {
		memcpy(sp, udpBuffer, sizeof(signalPacket));
		sp->network2host();
	}
	return sp->signal;
}
void setTimestampPacket(int id, double t1, double t2) {
	timestampPacket *tp = (timestampPacket*)udpBuffer;
	tp->packetId = htonl(id);
	tp->timestamp[0] = t1;
	tp->timestamp[1] = t2;
}
double getRate(int byte, double t) {
	/* return rate(Mbps) for sending byte in t second */
	return double(byte * 8) / t * 1e-6;
}
void tick() {
	clock_gettime(clockToUse, &programBegin);
}
void tock() {
	clock_gettime(clockToUse, &programEnd);
	double dt = timespec2double(programEnd) - timespec2double(programBegin);
	printf("program use %.2f s\n", dt);
}
