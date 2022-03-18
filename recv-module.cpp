/*
	
*/
#include "recv-module.h"
#include "util.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <limits>
/*
	listenFd: wait for sender
	connFd: fd for sender connection
	udpFd: fd for datagram
*/
int listenFd = -1, listenPort = 11106, connFd = -1, udpFd = -1, retransmitFlag = 1;
int repeatNumber = 1, retryNumber = 5, loadNumber = 100, loadSize = 1472, inspectNumber = 100, inspectSize = 1472, preheatNumber = 10, trainPacket, noUpdate;
socklen_t sockLen = 0;
sockaddr_in destAddress, srcAddress;
string timestampFilename("timestamp.txt"), resultFilename("result.txt"), logFilename("log.txt");
FILE *timeFile, *resultFile, *logFile;
int recvFlag = MSG_DONTWAIT, busyPoll = -1, runOnce = 1;
signalPacket sigPkt;
double inspectGap, loadInspectGap;
double beginTimeDouble, currentTimeDouble, tmpDouble;
double abw, abwLow, abwHigh;
double preheatTx[1000], preheatRx[1000];
const int loadMaxNumber = int(1e7);
double loadTx[loadMaxNumber], loadRx[loadMaxNumber], inspectTx[10000], inspectRx[10000];
double loadOWD[loadMaxNumber], inspectOWD[10000];
double lastMaxValue, lastMinValue, recoverThreshold;
const double minimalAbw = 50.0, maximalAbw = 960.0;
const double q = 0.01;
const int defaultLoadNumber = 100;
const int n1Global = 60;
const double defaultInspectGap = 400;
double th1 = 100, th2 = 300, th3 = 50;
struct description {
	double values[6]; // global min, global max, load min, load max, inspect min, inspect max
	int indexs[6]; // index of above values
	description() {
		for (int i = 0; i < 6; i++) {
			if (i % 2 == 0) {
				values[i] = std::numeric_limits<double>::max();
			} else {
				values[i] = std::numeric_limits<double>::lowest();
			}
			indexs[i] = -1;
		}
	}
};
void getDescription(struct description &a) {
	double temp;
	for (int i = 0; i < loadNumber; i++) {
		temp = loadOWD[i];
		if (temp < a.values[0]) {
			a.values[0] = temp;
			a.indexs[0] = i;
		}
		if (temp < a.values[2]) {
			a.values[2] = temp;
			a.indexs[2] = i;
		}
		if (temp > a.values[1]) {
			a.values[1] = temp;
			a.indexs[1] = i;
		}
		if (temp > a.values[3]) {
			a.values[3] = temp;
			a.indexs[3] = i;
		}
	}
	for (int i = 0; i < inspectNumber; i++) {
		temp = inspectOWD[i];
		if (temp < a.values[0]) {
			a.values[0] = temp;
			a.indexs[0] = i + loadNumber;
		}
		if (temp < a.values[4]) {
			a.values[4] = temp;
			a.indexs[4] = i + loadNumber;
		}
		if (temp > a.values[1]) {
			a.values[1] = temp;
			a.indexs[1] = i + loadNumber;
		}
		if (temp > a.values[5]) {
			a.values[5] = temp;
			a.indexs[5] = i + loadNumber;
		}
	}
	temp = a.values[0];
	for (int i = 0; i < 6; i++) {
		a.values[i] -= temp;
	}
}
void fprintDescription(struct description &a) {
	for (int i = 0; i < 6; i++) {
		fprintf(resultFile, "%6.0f %4d", a.values[i] * 1e6, a.indexs[i]);
		if (i != 5)
			fprintf(resultFile, " ");
		else
			fprintf(resultFile, "\n");
	}
}
void innerMain(int argc, char *argv[]){
	signal(SIGINT, safeExit);
	parseParameter(argc, argv);
	initialize();
	while (1) {
		/* a new sender connects */
		waitConnection();
		openFile();
		/* when a connection comes, get the param, get prepared */
		exchangeParameter();
		/* loop M times */
		mainReceive();
		/* close some socket and files */
		clean();
		printf("End of connection\n");
		if (runOnce == 1) 
			break;
	}
	safeExit(0);
}
void safeExit(int signal) {
	closeSocket();
	closeFile();
	printf("safe exit\n");
	exit(0);
}
void openFile() {
	logFile = fopen(logFilename.c_str(), "w");
	timeFile = fopen(timestampFilename.c_str(), "w");
	resultFile = fopen(resultFilename.c_str(), "w");
}
void qclose(FILE *&fp) {
	if (fp != 0) {
		fclose(fp);
		fp = 0;
	}
}
void closeFile(){
	qclose(timeFile);
	qclose(logFile);
	qclose(resultFile);
}
void closeSocket(){
	close(listenFd);
	close(connFd);
	close(udpFd);
}
/*
	-port [] -timestamp [] -result [] -log [] -polling [] -busy-poll [] -once -thres1 [] -thres2 [] -thres3 [] -retransmit []
	-port: 监听的端口
	-timestamp: 保存的时间戳
	-result: 输出的结果
	-log: 日志
	-polling: 启用立即返回的recv
	-busy-poll: SO_BUSY_POLL选项
	-once: 运行一次就退出
	-thres1: 阈值1
	-retransmit: 一列探测队列丢失后，是否重传该队列
*/
static struct option longOptions[] = {
	{"port",      required_argument, 0,  1},
	{"timestamp", required_argument, 0,  2},
	{"result",    required_argument, 0,  3},
	{"log",       required_argument, 0,  4},
	{"polling",   required_argument, 0,  5},
	{"busy-poll", required_argument, 0,  6},
	{"once",            no_argument, 0,  7},
	{"thres1",    required_argument, 0,  8},
	{"thres2",    required_argument, 0,  9},
	{"thres3",    required_argument, 0, 10},
	{"retransmit",    required_argument, 0, 11},
	{0,           0,                 0,  0}
};
void parseParameter(int argc, char *argv[]) {
	int optionIndex = 0, c;
    while ((c = getopt_long(argc, argv, "", longOptions, &optionIndex)) != -1) {
        switch (c) {
			case 1:
				listenPort = atoi(optarg);
				break;
			case 2:
				timestampFilename = string(optarg);
				break;
			case 3:
				resultFilename = string(optarg);
				break;
			case 4:
				logFilename = string(optarg);
				break;
			case 5:
				if (atoi(optarg) == 1) {
					recvFlag |= MSG_DONTWAIT;
				} else {
					recvFlag &= (~MSG_DONTWAIT);
				}
				break;
			case 6:
				busyPoll = atoi(optarg);
				break;
			case 7:
				runOnce = 1;
				break;
			case 8:
				th1 = atof(optarg);
				break;
			case 9:
				th2 = atof(optarg);
				break;
			case 10:
				th3 = atof(optarg);
				break;
			case 11:
				retransmitFlag = atoi(optarg);
			default:
				printf("getopt returned character code 0%o\n", c);
				break;
        }
    }
	printf("-------Parse Arg End-----------\n");
}

void initialize() {
	int ret;
	/* listen socket */
	listenFd = socket(AF_INET, SOCK_STREAM, 0); 
	if (listenFd == -1) {
		perror("receiver listen socket:");
	}
	ret = setReuseAddr(listenFd);
	if (ret) {
		perror("receiver tcp reuse:");
	}
	setSockaddr(&destAddress, listenPort);
	ret = bindAddress(listenFd, &destAddress); 
	if (ret) {
		perror("receiver bind:");
	}
	ret = listen(listenFd, 5);
	if (ret) {
		perror("receiver listen:");
	}
	/* udp receive socket */
	udpFd = socket(AF_INET, SOCK_DGRAM, 0);
	if (udpFd == -1) {
		perror("receiver udp socket:");
	}
	ret = setReuseAddr(udpFd);
	if (ret) {
		perror("receiver udp reuse:");
	}
	if (busyPoll >= 0)
		setBusyPoll(udpFd, busyPoll);
	bindAddress(udpFd, &destAddress);
	printf("-------Initialize End-----------\n");
}
void waitConnection(){
	/* connection socket */
	connFd = accept(listenFd, (sockaddr*)&srcAddress, &sockLen);
	if (connFd == -1) {
		perror("receiver accept:");
	}
}
void updateParam(const controlParameter& pkt) {
	repeatNumber = pkt.param[0];
	retryNumber = pkt.param[1];
	loadNumber = pkt.param[2];
	loadSize = pkt.param[3];
	inspectNumber = pkt.param[4];
	inspectSize = pkt.param[5];
	preheatNumber = pkt.param[6];
	noUpdate = pkt.param[7];
}
void exchangeParameter() {
	/* receive sender ctrlPacket */
	ssize_t ret;
	memset(udpBuffer, 0, sizeof(udpBuffer));
	ret = recv(connFd, udpBuffer, CONTROL_MESSAGE_LENGTH_1, 0);
	if (ret != CONTROL_MESSAGE_LENGTH_1) {
		printf("send control message %zd\n", ret);
		exit(0);
	}
	controlParameter ctrlPacket;
	memcpy(&ctrlPacket, udpBuffer, sizeof(controlParameter));
	ctrlPacket.network2host();
	/* update global parameters */
	updateParam(ctrlPacket);
	fprintf(logFile, "repeatNumber %d, retryNumber %d, loadNumber %d, loadSize %d, inspectNumber %d, inspectSize %d, preheatNumber %d\n", repeatNumber, retryNumber, loadNumber, loadSize, inspectNumber, inspectSize, preheatNumber);
	fprintf(logFile, "th1 %3.0f, th2 %3.0f, th3 %3.0f\n", th1, th2, th3);
	fprintf(logFile, "-----------Parameter Exchange End------------\n");
}
int recvPreheat(){
	return recvWithTimeout(0, inspectSize, preheatNumber, 1.0, preheatTx, preheatRx);
}
int recvWithTimeout(int offset, int ps, int pn, double timeout, double *a, double *b) {
	/* 
		if packet gap exceeds timeout, discard the train 
		如果timeout大于0，存在一个包：等待时间超过timeout或者包i的id不为i+offset，则返回1 
		时间保存在a,b中
	*/
	ssize_t ret;
	int i = 0, lossNumber = 0, nidx;
	beginTimeDouble = 0;
	while (i < pn) {
		ret = recv(udpFd, udpBuffer, ps, recvFlag);
		currentTimeDouble = getTimeDouble();
		if (ret == ps) {
			tpBuffer->network2host();
			if (tpBuffer->packetId == i + offset) {
				a[i] = tpBuffer->timestamp[0];
				b[i] = currentTimeDouble;
			} else {
				nidx = tpBuffer->packetId - offset;
				if (nidx > i) {
					lossNumber += nidx - i;
					fprintf(logFile, "loss %d->%d (current %d)\n", i, nidx, lossNumber);
				} else {
					fprintf(logFile, "order %d->%d\n", i, nidx);
					lossNumber++;
				}
				i = nidx;
				a[i] = tpBuffer->timestamp[0];
				b[i] = currentTimeDouble;
			}
			i++;
			beginTimeDouble = currentTimeDouble;
		} else if (timeout > 0 && beginTimeDouble > 0) {/* timeout */
			if (currentTimeDouble >= beginTimeDouble + timeout) {
				fprintf(logFile, "timeout at i %d, pn %d, timeout %.6f, dt %.6f\n", i, pn, timeout, currentTimeDouble - beginTimeDouble);
				break;
			}
		}
	}
	return lossNumber;
}
int recvTrain(){
	/* load, inspect */
	if (recvWithTimeout(0, loadSize, loadNumber, 0.5, loadTx, loadRx))
		return 1;
	if (recvWithTimeout(loadNumber, inspectSize, inspectNumber, 0.5, inspectTx, inspectRx))
		return 1;
	/* OWD */
	for (int i = 0; i < loadNumber; i++) {
		loadOWD[i] = loadRx[i] - loadTx[i];
	}
	for (int i = 0; i < inspectNumber; i++) {
		inspectOWD[i] = inspectRx[i] - inspectTx[i];
	}
	return 0;
}
double getX(double x1, double x2, double y1, double y2, double y) {
	double ret;
	if (y1 == y2) {
		return DoubleMax;
	} else {
		ret = x2 + (x1 - x2) * (y - y2) / (y1 - y2);
	}
	return ret;
}
void writeTrain(int stream, int train, FILE *fp){
	for (int i = 0; i < loadNumber; i++) {
		fprintf(fp, "%.6f,%.6f\n", loadTx[i], loadRx[i]);
	}
	for (int i = 0; i < inspectNumber; i++) {
		fprintf(fp, "%.6f,%.6f\n", inspectTx[i], inspectRx[i]);
	}
	fprintf(fp, "\n");
}
int isRecovered(int lastNumber, double th1, double th2, double th3) {
	/*
		最后一个包的单向延迟可以小于首个包的单向延迟，但是不能大th1
		最后lastNumber的最大值和最小值差小于th2
		使用首个小于max(maxV,minV+th3*1e-6)作为恢复的下标
	*/
	assert(lastNumber < inspectNumber);
	bool cond1 = inspectOWD[inspectNumber - 1] < loadOWD[0] + th1 * 1e-6;
	double maxValue, minValue;
	maxValue = minValue = inspectOWD[inspectNumber - 1];
	int idx;
	for (int i = inspectNumber - lastNumber; i < inspectNumber; i++) {
		idx = i;
		if (inspectOWD[idx] > maxValue)
			maxValue = inspectOWD[idx];
		if (inspectOWD[idx] < minValue)
			minValue = inspectOWD[idx];
	}
	bool cond2 = maxValue < minValue + th2 * 1e-6;
	lastMaxValue = maxValue;
	lastMinValue = minValue;
	/* 避免后10个owd的值非常集中 */
	if (maxValue - minValue >= th3 * 1e6)
		recoverThreshold = maxValue;
	else
		recoverThreshold = minValue + th3 * 1e-6;
	fprintf(logFile, "isRecovered cond1 %s, cond2 %s\n", cond1 ? "True" : "False", cond2 ? "True" : "False");
	return cond1 && cond2;
}
void getEstimation(int trainIdx, double &abwLow, double &abwHigh, double &abwEstimation) {
	/*
		下标ridx：单向延迟小于等于lastMaxValue的首个包下标（一定存在）
		abwLow：ridx
		abwHigh：ridx-1
		abwEstimation：ridx-2,ridx-1与mean[ridx:]的交点是否在ridx和ridx-1内；否则取时间平均计算
	*/
	int ridx = 0;
	double a;
	for (int i = 0; i < inspectNumber; i++) {
		a = inspectOWD[i];
		if (a <= recoverThreshold) {
			ridx = i;
			break;
		}
	}
	fprintf(logFile, "ridx %d\n", ridx);
	if (ridx == 0) {
		/* 可用带宽大于当前检查包所处的范围 */
		int byte = loadNumber * loadSize;
		double tLow = loadTx[loadNumber - 1] - loadTx[0];
		double tHigh = inspectTx[0] - loadTx[0];
		abwLow = getRate(byte, tHigh);
		abwHigh = getRate(byte, tLow);
		abwEstimation = abwLow;
	} else {
		int leftLow = (ridx - 2), Low = (ridx - 1), High = ridx;
		double tLow = inspectTx[Low];
		double tHigh = inspectTx[High];
		double mean = 0;
		double tLeftLow, tX, tBegin = loadTx[0];
		int byte1, byte2;
		byte1 = loadNumber * loadSize + (ridx - 1) * inspectSize;
		byte2 = byte1 + inspectSize;	
		for (int i = ridx; i < inspectNumber; i++) {
			mean += inspectOWD[i];
		}
		if (inspectNumber != ridx) {
			mean /= (inspectNumber - ridx);
			if (ridx > 1) {
				tLeftLow = inspectTx[leftLow];
				tX = getX(tLeftLow, tLow, inspectOWD[leftLow], inspectOWD[Low], mean);
				if (tX < tLow || tX > tHigh) {
					tX = (tLow + tHigh) / 2;
				}
			} else {
				tX = (tLow + tHigh) / 2;
			}	
			tX -= tBegin;
			tHigh -= tBegin;
			tLow -= tBegin;
			fprintf(logFile, "tX %.0f tLow %.0f tHigh %.0f\n", tX * 1e6, tLow * 1e6, tHigh * 1e6);
			fprintf(logFile, "byte1 %d byte2 %d\n", byte1, byte2);
			abwEstimation = getRate(byte1, tX);
			abwLow = getRate(byte2, tHigh);
			abwHigh = getRate(byte1, tLow);
			if (abwEstimation < abwLow) {
				abwEstimation = (abwLow + abwHigh) / 2;
				fprintf(logFile, "middle abw %.2f\n", abwEstimation);
			}
		}
	}
}
void getLN_G(double abwEstimation, double q, int &LN, double &G, int n1) {
	/*
		LN:loadNumber 10-100
		G:inspectGap 20-400
		P=(LN+IN/2)
		x=P/A
		G=qx
		G=qP/A=q(LN+IN/2)*p/A
		故G和LN是正相关，
		当A较小时，G可能太小，导致q偏小，适当减小LN
		当A较大时，LN可能偏小，导致q偏大，适当减小G
		令LN=100,得到G，若G<40，令G=40,求得LN
	*/
	LN = 100;
	double y = n1 * inspectSize;
	G = q * (LN * loadSize + y) * 8 / abwEstimation;
	if (G < 40) {
		G = 40;
		LN = (G * abwEstimation / q / 8 - y) / loadSize;
		if (LN < 100) {
			LN = 100;
			fprintf(logFile, "check 1 %.2f %.2f\n", G, abwEstimation);
		} else if (LN > 400) {
			LN = 400;
			fprintf(logFile, "check 2 %.2f %.2f\n", G, abwEstimation);
		}
	} else if (G > 400) {
		G = defaultInspectGap;
		LN = defaultLoadNumber;
	}
	fprintf(logFile, "getLN_G(%.0f)=(%d,%.2f)\n", abwEstimation, LN, G);
}
void resetLN_G(int &LN, double &G, double &abw){
	LN = defaultLoadNumber;
	G = defaultInspectGap;
	abw = minimalAbw;
}
void resetBuffer(){
	memset(loadTx, 0, sizeof(loadTx));
	memset(loadRx, 0, sizeof(loadRx));
	memset(inspectTx, 0, sizeof(inspectTx));
	memset(inspectRx, 0, sizeof(inspectRx));
}
void mainReceive(){
	/* 预热阶段 */
	if (preheatNumber > 0) {
		int preheatRepeat = 0;
		int preheatLost = 0;
		sigPkt = {ctrlSignal::preheat, 0, 0, 0};
		do {
			if (preheatLost)
				sigPkt = {ctrlSignal::preheatTimeout, 0, 0, 0};
			sendSignal(connFd, &sigPkt);
			preheatLost = recvPreheat();
			preheatRepeat++;
		} while (preheatLost);
	}
	/* 循环阶段 */
	int streamNumber = 0, trainNumber = 0, trainLost = 0, exitFlag = 0, recoverFlag = 0, specialFlag = 1;
	abw = minimalAbw;
	inspectGap = defaultInspectGap;
	sigPkt = {ctrlSignal::firstTrain, loadNumber, abw, inspectGap, specialFlag};
	int lastNumber = 10;
	double txLast, rxLast;
	while (1) {
		resetBuffer();
		sendSignal(connFd, &sigPkt);
		if (exitFlag == 1)
			break;
		trainLost = recvTrain();
		if (trainLost == 1) {
			if (retransmitFlag == 1) {
				sigPkt = {ctrlSignal::retransmit, 0, 0, 0};
				fprintf(logFile, "train lost detected\n");
				timespec tempTime = double2timespec(0.5);
				clock_nanosleep(clockToUse, 0, &tempTime, NULL);
			} else {
				fprintf(logFile, "=====stream %d, retry %d=======\n", streamNumber, trainNumber);
				writeTrain(streamNumber, trainNumber, timeFile);
				goto successPoint;
			}
		} else if (trainLost == 0) {
			fprintf(logFile, "=====stream %d, retry %d=======\n", streamNumber, trainNumber);
			if (inspectNumber == 0) {
				txLast = loadTx[loadNumber - 1];
				rxLast = loadRx[loadNumber - 1];
			} else {
				txLast = inspectTx[inspectNumber - 1];
				rxLast = inspectRx[inspectNumber - 1];
			}
			fprintf(logFile, "send [%.0f], recv [%.0f]\n", (txLast - loadTx[0]) * 1e6, (rxLast - loadRx[0]) * 1e6);
			writeTrain(streamNumber, trainNumber, timeFile);
			if (inspectNumber > 0)
				recoverFlag = isRecovered(lastNumber, th1, th2, th3);
			else
				recoverFlag = false;
			if (recoverFlag) {
				abwLow = minimalAbw;
				abwHigh = maximalAbw;
				abw = minimalAbw;
				getEstimation(streamNumber, abwLow, abwHigh, abw);
				fprintf(logFile, "Estimation: [%.0f, %.0f], %.0f\n", abwLow, abwHigh, abw);
				if (abw > 0 && !noUpdate)
					getLN_G(abw, q, loadNumber, inspectGap, n1Global);
				specialFlag = 0;
			} else if (!noUpdate) {
				resetLN_G(loadNumber, inspectGap, abw);
				fprintf(logFile, "resetDefault\n");
				specialFlag = 1;
			}
			if (recoverFlag) {
				fprintf(resultFile, "%4d %6.2f %6.2f %6.2f ", streamNumber, abw, abwLow, abwHigh);
			} else {
				fprintf(resultFile, "%4d %6.2f %6.2f %6.2f ", streamNumber, -1.0, -1.0, -1.0);
			}
			{	struct description a;
				getDescription(a);
				fprintDescription(a);}
successPoint:
			trainNumber++;
			if (trainNumber < retryNumber) {
				sigPkt = {ctrlSignal::nextTrain, loadNumber, abw, inspectGap, specialFlag};
			} else {
				streamNumber++;
				if (streamNumber < repeatNumber) {
					trainNumber = 0;
					sigPkt = {ctrlSignal::nextStream, loadNumber, abw, inspectGap, specialFlag};
				} else {
					sigPkt = {ctrlSignal::end, 0, 0, 0, 0};
					exitFlag = 1;
				}
			}
		}
	}
	fprintf(resultFile, "\n");
}
void clean() {
	close(connFd);
	closeFile();
}