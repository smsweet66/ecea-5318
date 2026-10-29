/*
 *
 *  Adapted by Sam Siewert for use with UVC web cameras and Bt878 frame
 *  grabber NTSC cameras to acquire digital video from a source,
 *  time-stamp each frame acquired, save to a PGM or PPM file.
 *
 *  The original code adapted was open source from V4L2 API and had the
 *  following use and incorporation policy:
 *
 *  This program can be used and distributed without restrictions.
 *
 *      This program is provided with the V4L2 API
 * see http://linuxtv.org/docs.php for more information
 */

#include "util.hpp"

#include <assert.h>
#include <errno.h>
#include <fcntl.h> /* low-level i/o */
#include <getopt.h>
#include <linux/videodev2.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <syslog.h>
#include <time.h>
#include <cstdio>
#include <string>
#include <vector>

#define READ_FRAME_PERIOD_NS 40000000

namespace {

static void mainloop(mqd_t messageQueue, int fd, int frameCount,
                     const std::vector<util::Buffer>& buffers) {
    timespec readDelay;
    timespec timeError;

    readDelay.tv_sec = 0;
    readDelay.tv_nsec = READ_FRAME_PERIOD_NS;

    unsigned int count = frameCount;

    while (count > 0) {
        printf("count: %u\n", count);
        for (;;) {
            fd_set fds;
            struct timeval tv;
            int r;

            FD_ZERO(&fds);
            FD_SET(fd, &fds);

            /* Timeout. */
            tv.tv_sec = 2;
            tv.tv_usec = 0;

            r = select(fd + 1, &fds, NULL, NULL, &tv);

            if (-1 == r) {
                if (EINTR == errno)
                    continue;
                util::errnoExit("select");
            }

            if (0 == r) {
                fprintf(stderr, "select timeout\n");
                exit(EXIT_FAILURE);
            }

            if (util::readFrame(messageQueue, buffers, fd)) {
                if (clock_nanosleep(CLOCK_MONOTONIC, 0, &readDelay,
                                    &timeError) != 0)
                    perror("nanosleep");
                else
                    printf("time_error.tv_sec=%ld, time_error.tv_nsec=%ld\n",
                           timeError.tv_sec, timeError.tv_nsec);

                count--;
                break;
            }
        }
    }
}

static void usage(FILE* fp, char** argv, const std::string& devName,
                  int frameCount) {
    fprintf(fp,
            "Usage: %s [options]\n\n"
            "Version 1.3\n"
            "Options:\n"
            "-d | --device name   Video device name [%s]\n"
            "-h | --help          Print this message\n"
            "-m | --mmap          Use memory mapped buffers [default]\n"
            "-r | --read          Use read() calls\n"
            "-u | --userp         Use application allocated buffers\n"
            "-o | --output        Outputs stream to stdout\n"
            "-f | --format        Force format to 640x480 GREY\n"
            "-c | --count         Number of frames to grab [%i]\n"
            "",
            argv[0], devName.c_str(), frameCount);
}

static const char shortOptions[] = "d:hmruofc:";

static const struct option longOptions[] = {
    {"device", required_argument, NULL, 'd'},
    {"help", no_argument, NULL, 'h'},
    {"mmap", no_argument, NULL, 'm'},
    {"read", no_argument, NULL, 'r'},
    {"userp", no_argument, NULL, 'u'},
    {"output", no_argument, NULL, 'o'},
    {"format", no_argument, NULL, 'f'},
    {"count", required_argument, NULL, 'c'},
    {0, 0, 0, 0}};

}  // namespace

int main(int argc, char** argv) {
    struct v4l2_format fmt;

    std::string devName;
    int fd = -1;
    std::vector<util::Buffer> buffers;
    int frameCount = 30;

    if (argc > 1)
        devName = argv[1];
    else
        devName = "/dev/video0";

    for (;;) {
        int idx;
        int c;

        c = getopt_long(argc, argv, shortOptions, longOptions, &idx);

        if (-1 == c)
            break;

        switch (c) {
            case 0: /* getopt_long() flag */
                break;

            case 'd':
                devName = optarg;
                break;

            case 'h':
                usage(stdout, argv, devName, frameCount);
                exit(EXIT_SUCCESS);

            case 'c':
                errno = 0;
                frameCount = strtol(optarg, NULL, 0);
                if (errno)
                    util::errnoExit(optarg);
                break;

            default:
                usage(stderr, argv, devName, frameCount);
                exit(EXIT_FAILURE);
        }
    }

    mq_attr attr = {
        0,
        10,
        sizeof(util::Buffer),
        0,
    };

    mqd_t const messageQueue =
        mq_open("/image_queue", O_CREAT | O_RDWR, 0644, &attr);
    if (messageQueue == -1) {
        std::perror("mq_open");
        return 1;
    }

    pthread_t thread;

    util::ProcessImagesArgs args = {
        messageQueue,
        frameCount,
        &fmt,
    };

    pthread_create(&thread, NULL, util::processImages, &args);
    util::openDevice(&fd, devName);
    util::initDevice(fd, devName, &fmt, &buffers);
    util::startCapturing(fd, buffers.size());
    mainloop(messageQueue, fd, frameCount, buffers);
    util::stopCapturing(fd);
    util::uninitDevice(&buffers);
    util::closeDevice(fd);
    fprintf(stderr, "\n");

    pthread_join(thread, NULL);
    mq_close(messageQueue);

    return 0;
}
