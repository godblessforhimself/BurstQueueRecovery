/*
	preheat packet:
	预热包，preheatNumber个，loadSize的包，间隔preheatGap微秒
	preheat发送失败会重发

	stream:
	数量repeatNumber，间隔streamGap；每个stream包含最多retryNumber个train。train-train的间隔为trainGap。
	train发送失败会重发
	
	train:
	包含负载包和检查包。负载包数量loadNumber，大小loadSize，速率loadRate；检查包数量inspectNumber，大小inspectSize，与负载包间隔loadInspectGap，间隔inspectGap。

	严格按照参数进行发送，参数由接收端程序进行设置；
	需要设置的参数：负载包数量ln、检查包间隔G、可用带宽估计abw
*/
#include "send-module.h"
#include "util.h"
#include <algorithm>

int tcpFd = -1, udpFd = -1, destPort = 11106, globalPacketId = 0, noUpdate = 1;
int repeatNumber = 1, retryNumber = 1, loadNumber = 100, loadSize = 1472, inspectNumber = 100, inspectSize = 1472, preheatNumber = 0;
double loadRate = 1000, streamGap = 10000, trainGap = 1000, preheatGap = 1000, inspectGap = 350, loadInspectGap = 40;
double minimalAbw = 50, maximumAbw = 1000, abw = 50;
int n1Global = 80, n2Global = 20;
double minGapGlobal = 40, maxGapGlobal = 400, GapGlobal = 300;
char destIPString[20] = "10.0.7.1";
sockaddr_in srcAddress, destAddress;
socklen_t sockLen = sizeof(sockaddr_in);
signalPacket sigPkt;
timespec beginTime, currentTime, endTime, tempTime;
double beginTimeDouble, currentTimeDouble, endTimeDouble, timeDouble, actualRate, tmpDouble;
timestampPacket tpacketTemp;
double offsetLoadTime[int(1e7)];
double offsetInspectTime[10000];
int debugOption = 0;
/*
	loadRate: 负载包速率
	loadSize: 负载包尺寸
	inspectSize: 检查包尺寸
	loadNumber: 负载包数量
	inspectNumber: 检查包数量
	repeatNumber: 探测重复次数
	retryNumber: 当一次探测无法计算出ABW时，继续重复的最大次数
	preheatNumber: 预热包数量
	inspectGap: 弃用
	loadInspectGap: 弃用
	streamGap: 队列尾到首的间隔
	trainGap: retry或retransmit的间隔
	preheatGap: 预热包的间隔
	port: 服务器的端口
	dest: 服务器的IP
	noUpdate: 是否自动调节队列
	minAbw: 最小可用带宽
	minGap: 发出包之间允许的最小间隔
	maxGap: 发出包间允许的最大间隔
	maxAbw: 最大可用带宽
	Gap: n2个检查包与前一个包的发送间隔
	n1: 检查包的第一部分数量
	n2: 检查包的第二部分数量
*/
static struct option longOptions[] = {
	{"loadRate",   required_argument, 0, 1},
	{"loadSize",    required_argument, 0,  2},
	{"inspectSize",    required_argument, 0,  3},
	{"loadNumber",  required_argument, 0,  4},
	{"inspectNumber",    required_argument, 0,  5},
	{"repeatNumber",    required_argument, 0,  7},
	{"retryNumber",    required_argument, 0,  8},
	{"preheatNumber",    required_argument, 0,  9},
	{"inspectGap",    required_argument, 0,  10},
	{"loadInspectGap",    required_argument, 0,  11},
	{"streamGap",    required_argument, 0,  12},
	{"trainGap",    required_argument, 0,  13},
	{"preheatGap",    required_argument, 0,  14},
	{"port",    required_argument, 0,  15},
	{"dest",    required_argument, 0,  16},
	{"noUpdate",required_argument, 0,  17},
	{"minAbw", required_argument, 0,  18},
	{"minGap", required_argument, 0,  19},
	{"maxGap", required_argument, 0,  20},
	{"debug", required_argument, 0,  21},
	{"maxAbw", required_argument, 0,  22},
	{"Gap", required_argument, 0,  23},
	{"n1", required_argument, 0,  24},
	{"n2", required_argument, 0,  25},
	{0,         0,                 0,  0}
};
void internalArrange(int byte, double leftAbw, double rightAbw, int npoint, int offset, double minGap, double maxGap, double lastT) {
	if (npoint <= 0) {
		return;
	}
	double q = 1.0 / npoint;
	double nextAbw = q * rightAbw + (1.0 - q) * leftAbw;
	double nextT = byte * 8 / nextAbw * 1e-6;
	double currentGap = nextT - lastT;
	if (currentGap <= maxGap && currentGap >= minGap) {
		offsetInspectTime[offset] = nextT;
		internalArrange(byte + inspectSize, nextAbw, rightAbw, npoint - 1, offset + 1, minGap, maxGap, nextT);
	} else if (currentGap > maxGap) {
		for (int i = 0; i < npoint; i++) {
			offsetInspectTime[offset + i] = lastT + maxGap * (i + 1);
		}
	} else if (currentGap < minGap) {
		nextT = lastT + minGap;
		nextAbw = byte * 8 / nextT * 1e-6;
		offsetInspectTime[offset] = nextT;
		internalArrange(byte + inspectSize, nextAbw, rightAbw, npoint - 1, offset + 1, minGap, maxGap, nextT);
	}
}
/*
	速率单位Mbps，时间单位s，大小单位字节 
	前n1个点在minGap和maxGap的限制下均分minA和maxA，可超过目标时间，但不会小于目标时间；
	后n2个点间隔为Gap
*/
void arrange(double minA, double maxA, double minGap, double maxGap, double Gap, int n1, int n2, int loadNumber, int loadSize, double loadRate, int inspectSize){
	for (int i = 0; i < loadNumber; i++) {
		offsetLoadTime[i] = loadSize * i * 8 / loadRate * 1e-6;
	}
	if (n1 == 0 && n2 == 0) {
		return;
	}
	int loadByte = loadNumber * loadSize;
	offsetInspectTime[0] = loadByte * 8 / maxA * 1e-6;
	internalArrange(loadByte + inspectSize, maxA, minA, n1 - 1, 1, minGap, maxGap, offsetInspectTime[0]);
	for (int i = 0; i < n2; i++) {
		offsetInspectTime[i + n1] = offsetInspectTime[i + n1 - 1] + Gap;
	}
	if (debugOption) {
		for (int i = 0; i < loadNumber; i++) {
			printf("%3d: %6.0f\n", i, offsetLoadTime[i] * 1e6);
		}
		for (int i = 0; i < n1 + n2; i++) {
			printf("%3d: %6.0f\n", i, offsetInspectTime[i] * 1e6);
		}
	}
	printf("Duration %.0f\n", (offsetInspectTime[inspectNumber - 1] - offsetLoadTime[0]) * 1e6);
}
int parseArgs(int argc, char *argv[]) {
	int optionIndex = 0, c;
    while ((c = getopt_long(argc, argv, "", longOptions, &optionIndex)) != -1) {
        switch (c) {
            case 1:
				loadRate = atof(optarg);
				break;
			case 2:
				loadSize = atoi(optarg);
				break;
			case 3:
				inspectSize = atoi(optarg);
				break;
			case 4:
				loadNumber = atoi(optarg);
				break;
			case 5:
				inspectNumber = atoi(optarg);
				break;
			case 7:
				repeatNumber = atoi(optarg);
				break;
			case 8:
				retryNumber = atoi(optarg);
				break;
			case 9:
				preheatNumber = atoi(optarg);
				break;
			case 10:
				inspectGap = atof(optarg);
				break;
			case 11:
				loadInspectGap = atof(optarg);
				break;
			case 12:
				streamGap = atof(optarg);
				break;
			case 13:
				trainGap = atof(optarg);
				break;
			case 14:
				preheatGap = atof(optarg);
				break;
			case 15:
				destPort = atoi(optarg);
				break;
			case 16:
				strncpy(destIPString, optarg, sizeof(destIPString));
				break;
			case 17:
				noUpdate = atoi(optarg);
				break;
			case 18:
				minimalAbw = atof(optarg);
				break;
			case 19:
				minGapGlobal = atof(optarg);
				break;
			case 20:
				maxGapGlobal = atof(optarg);
				break;
			case 21:
				debugOption = atoi(optarg);
				break;
			case 22:
				maximumAbw = atof(optarg);
				break;
			case 23:
				GapGlobal = atof(optarg);
				break;
			case 24:
				n1Global = atoi(optarg);
				break;
			case 25:
				n2Global = atoi(optarg);
				break;
			default:
				printf("getopt returned character code 0%o\n", c);
				break;
        }
    }
	int minSize=(int)sizeof(timestampPacket);
	if (loadSize < minSize) {
		printf("load packet size %d < min %d\n", loadSize, minSize);
		loadSize = minSize;
	}
	if (inspectSize < minSize) {
		printf("inspect packet size %d < min %d\n", inspectSize, minSize);
		inspectSize = minSize;
	}
	if (inspectNumber != n1Global + n2Global) {
		printf("inspectNumber %d <-> n1+n2 %d\n", inspectNumber, n1Global+n2Global);
		inspectNumber = n1Global + n2Global;
	}
	arrange(minimalAbw, maximumAbw, minGapGlobal * 1e-6, maxGapGlobal * 1e-6, GapGlobal * 1e-6, n1Global, n2Global, loadNumber, loadSize, loadRate, inspectSize);
	printf("loadRate %.2f, recv %s: %d\n", loadRate, destIPString, destPort);
	printf("-------Parse Arg End---------\n");
    return 0;
}
void initialize() {
	setSockaddr(&destAddress, destIPString, destPort);
	setSockaddr(&srcAddress, destPort + 1);
	tcpFd = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(tcpFd, (sockaddr*)&destAddress, sockLen)) {
		perror("sender tcp connect:");
	}
	udpFd = socket(AF_INET, SOCK_DGRAM, 0);
	bindAddress(udpFd, &srcAddress);
	if (connect(udpFd, (sockaddr*)&destAddress, sockLen)) {
		perror("sender udp connect:");
	}
	printf("--------Initialize End---------\n");
}
void exchangeParameter() {
	/* 
	repeatNumber, retryNumber, loadNumber, loadSize, inspectNumber, inspectSize
	loadRate, duration
	*/
	ssize_t ret;
	controlParameter ctrlPacket = {
		repeatNumber,
		retryNumber,
		loadNumber,
		loadSize,
		inspectNumber,
		inspectSize,
		preheatNumber,
		noUpdate
	};
	ctrlPacket.host2network();
	memset(udpBuffer, 0, sizeof(udpBuffer));
	memcpy(udpBuffer, &ctrlPacket, sizeof(controlParameter));
	ret = send(tcpFd, udpBuffer, CONTROL_MESSAGE_LENGTH_1, 0);
	if (ret != CONTROL_MESSAGE_LENGTH_1) {
		printf("send control message %zd\n", ret);
		exit(0);
	}
	printf("repeatNumber %d, retryNumber %d, loadNumber %d, loadSize %d, inspectNumber %d, inspectSize %d\n", repeatNumber, retryNumber, loadNumber, loadSize, inspectNumber, inspectSize);
	printf("-----------Parameter Exchange End------------\n");
}
void clean() {
	close(tcpFd);
	close(udpFd);
}
void sendPreheat(){
	/* (preheatNumber,inspectSize) packet with preheatGap us */
	if (preheatNumber <= 0) {
		return;
	}
	globalPacketId = 0;
	clock_gettime(clockToUse, &beginTime);
	beginTimeDouble = timespec2double(beginTime);
	setTimestampPacket(globalPacketId++, beginTimeDouble, 0);
	send(udpFd, udpBuffer, inspectSize, 0);
	timeDouble = beginTimeDouble + preheatGap * 1e-6;
	int i = 1;
	while (i < preheatNumber) {
		clock_gettime(clockToUse, &currentTime);
		currentTimeDouble = timespec2double(currentTime);
		if (currentTimeDouble >= timeDouble) {
			setTimestampPacket(globalPacketId++, currentTimeDouble, 0);
			send(udpFd, udpBuffer, inspectSize, 0);
			timeDouble += preheatGap * 1e-6;
			i++;
		}
	}
}
void sendLoad(){
	/* (loadNumber,loadSize) packet at loadRate, end at time END */
	if (loadNumber <= 0) {
		return;
	}
	globalPacketId = 0;
	clock_gettime(clockToUse, &beginTime);
	beginTimeDouble = timespec2double(beginTime);
	setTimestampPacket(globalPacketId++, beginTimeDouble, 0);
	send(udpFd, udpBuffer, loadSize, 0);
	int i = 1;
	while (i < loadNumber) {
		clock_gettime(clockToUse, &currentTime);
		currentTimeDouble = timespec2double(currentTime);
		if (currentTimeDouble >= offsetLoadTime[i] + beginTimeDouble) {
			setTimestampPacket(globalPacketId++, currentTimeDouble, 0);
			send(udpFd, udpBuffer, loadSize, 0);
			i++;
		}
	}
	endTimeDouble = currentTimeDouble;
}
void sendInspect(){
	if (inspectNumber <= 0) {
		return;
	}
	for (int i = 0; i < inspectNumber;) {
		clock_gettime(clockToUse, &currentTime);
		currentTimeDouble = timespec2double(currentTime);
		if (currentTimeDouble >= offsetInspectTime[i] + beginTimeDouble) {
			setTimestampPacket(globalPacketId++, currentTimeDouble, 0);
			send(udpFd, udpBuffer, inspectSize, 0);
			i++;
		}
	}
}
void smartSleep(double t) {
	/* sleep t us; if t is small, use loop; if t is large, use clock_nanosleep */
	clock_gettime(clockToUse, &beginTime);
	beginTimeDouble = timespec2double(beginTime);
	if (t <= 0) {
		return;
	} else if (t > 100) {
		tempTime = double2timespec(t * 1e-6);
		clock_nanosleep(clockToUse, 0, &tempTime, NULL);
	}
	while (1) {
		clock_gettime(clockToUse, &currentTime);
		currentTimeDouble = timespec2double(currentTime);
		if (currentTimeDouble >= beginTimeDouble + t * 1e-6){
			break;
		}
	}
	return;
}
void mainSend(){
	/*
		repeatNumber streams separated by streamGap
		<=retryNumber trains controlled by receiver signal
		one train:
			(loadNumber,loadSize) packet at loadRate, end at time END
			(inspectNumber,inspectSize) packet evenly spaced between (END,duration)
		signals by receiver:
			1. next stream
			2. next retry followed by new duration parameter
			3. retransmit train with same parameter		
	*/
	int signal;
	while (1) {
		signal = waitSignal(tcpFd, &sigPkt);
		if (signal == ctrlSignal::preheat || signal == ctrlSignal::preheatTimeout) {
			sendPreheat();
			continue;
		} else if (signal == ctrlSignal::firstTrain) {
			;
		} else if (signal == ctrlSignal::nextTrain) {
			smartSleep(trainGap);
		} else if (signal == ctrlSignal::nextStream) {
			smartSleep(streamGap);
		} else if (signal == ctrlSignal::retransmit) {
			smartSleep(trainGap);
		} else if (signal == ctrlSignal::end) {
			break;
		}
		sendLoad();
		sendInspect();
	}
}
void innerMain(int argc, char *argv[]){
	parseArgs(argc, argv);
	/* initialize tcp and udp socket */
	initialize();
	/* exchange parameters */
	exchangeParameter();
	/* begin to send main */
	mainSend();
	clean();
}