#pragma once

#include <linux/videodev2.h>
#include <mqueue.h>
#include <ctime>
#include <string>
#include <vector>

namespace util {

struct ProcessImagesArgs {
    const mqd_t messageQueue;
    const int numFrames;
    const v4l2_format* const format;
};

struct Buffer {
    void* start;
    size_t length;
};

void errnoExit(const char* s);

int xioctl(int fh, int request, void* arg);

void dumpPpm(const void* p, int size, unsigned int tag, timespec* time);

void dumpPgm(const void* p, int size, unsigned int tag, timespec* time);

void yuv2rgb(int y, int u, int v, unsigned char* r, unsigned char* g,
             unsigned char* b);

void processImage(const void* p, size_t size, int frameCount,
                  const v4l2_format* fmt);

int readFrame(mqd_t messageQueue, const std::vector<Buffer>& buffers,
              int fileDescriptor);

void* processImages(void* arg);

void stopCapturing(int fd);

void startCapturing(int fd, unsigned int numBuffers);

void uninitDevice(std::vector<Buffer>* buffers);

void initMmap(int fd, const std::string& devName, std::vector<Buffer>* buffers);

void initDevice(int fd, const std::string& devName, v4l2_format* format,
                std::vector<Buffer>* buffers);

void closeDevice(int fd);

void openDevice(int* fd, const std::string& devName);

};  // namespace util
