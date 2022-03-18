efficient cpp version

### features:
* retry a maximum N times to get best results
* sender run M measurements with gap G
* sender reuse last measurements information
* receiver keeps running after sender finishs
* use K inspect packets in a burst to remove noise

### build
cd build
cmake ../
make

### run
receiver$ ./recv-main --timestamp timestamp.txt --result result.txt --log log.txt --polling 1 --busy-poll -1


#### send 100 stream with 5 retries
sender$ ./send-main --loadRate 1200 --loadSize 1472 --inspectSize 1472 --loadNumber 100 --inspectNumber 10 --inspectJumbo 1 --repeatNumber 100 --retryNumber 5 --preheatNumber 100 --duration 25000 --streamGap 25000 --trainGap 25000 --preheatGap 1000 --dest 10.0.7.1

#### send 1 stream with 1 retry
sender$ ./send-main --loadRate 1200 --loadSize 1472 --inspectSize 1472 --loadNumber 100 --inspectNumber 10 --inspectJumbo 1 --repeatNumber 1 --retryNumber 1 --preheatNumber 100 --duration 25000 --streamGap 25000 --trainGap 25000 --preheatGap 1000 --dest 10.0.7.1

#### constant rate
./send-main --loadRate 10 --loadSize 1472 --inspectSize 1472 --loadNumber 1000 --inspectNumber 0 --inspectJumbo 0 --repeatNumber 10 --retryNumber 1 --preheatNumber 0 --duration 0 --streamGap 10000 --trainGap 0 --preheatGap 0 --dest 10.0.7.1
