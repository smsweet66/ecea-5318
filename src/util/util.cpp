#include "util.hpp"
#include <bits/time.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <mqueue.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <time.h>
#include <unistd.h>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace util {

void errnoExit(const char* s) {
    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

int xioctl(int fh, int request, void* arg) {
    int r;

    do {
        r = ioctl(fh, request, arg);
    } while (-1 == r && EINTR == errno);

    return r;
}

void dumpPpm(const void* p, int size, unsigned int tag, timespec* time) {
    std::string ppmHeader =
        "P6\n#9999999999 sec 9999999999 msec \n320 240\n255\n";
    std::string ppmDumpName = "test00000000.ppm";

    snprintf(&ppmDumpName[4], 9, "%08d", tag);
    strncat(&ppmDumpName[12], ".ppm", 5);
    int const dumpfd =
        open(ppmDumpName.c_str(), O_WRONLY | O_NONBLOCK | O_CREAT, 00666);

    snprintf(&ppmHeader[4], 11, "%010d", (int)time->tv_sec);
    strncat(&ppmHeader[14], " sec ", 5);
    snprintf(&ppmHeader[19], 11, "%010d", (int)((time->tv_nsec) / 1000000));
    strncat(&ppmHeader[29], " msec \n320 240\n255\n", 19);

    // subtract 1 because sizeof for string includes null terminator
    write(dumpfd, ppmHeader.c_str(), sizeof(ppmHeader) - 1);

    size_t total = 0;

    do {
        const size_t written = write(dumpfd, p, size);
        total += written;
    } while (total < size);

    printf("wrote %zu bytes\n", total);

    close(dumpfd);
}

void dumpPgm(const void* p, int size, unsigned int tag, timespec* time) {
    std::string pgmHeader =
        "P5\n#9999999999 sec 9999999999 msec \n320 240\n255\n";
    std::string pgmDumpname = "test00000000.pgm";

    snprintf(&pgmDumpname[4], 9, "%08d", tag);
    strncat(&pgmDumpname[12], ".pgm", 5);
    int const dumpfd =
        open(pgmDumpname.c_str(), O_WRONLY | O_NONBLOCK | O_CREAT, 00666);

    snprintf(&pgmHeader[4], 11, "%010d", (int)time->tv_sec);
    strncat(&pgmHeader[14], " sec ", 5);
    snprintf(&pgmHeader[19], 11, "%010d", (int)((time->tv_nsec) / 1000000));
    strncat(&pgmHeader[29], " msec \n320 240\n255\n", 19);

    // subtract 1 because sizeof for string includes null terminator
    write(dumpfd, pgmHeader.c_str(), sizeof(pgmHeader) - 1);

    size_t total = 0;

    do {
        const size_t written = write(dumpfd, p, size);
        total += written;
    } while (total < size);

    printf("wrote %zu bytes\n", total);

    close(dumpfd);
}

// This is probably the most acceptable conversion from camera YUYV to RGB
//
// Wikipedia has a good discussion on the details of various conversions and
// cites good references: http://en.wikipedia.org/wiki/YUV
//
// Also http://www.fourcc.org/yuv.php
//
// What's not clear without knowing more about the camera in question is how
// often U & V are sampled compared to Y.
//
// E.g. YUV444, which is equivalent to RGB, where both require 3 bytes for each
// pixel
//      YUV422, which we assume here, where there are 2 bytes for each pixel,
//      with two Y samples for one U & V,
//              or as the name implies, 4Y and 2 UV pairs
//      YUV420, where for every 4 Ys, there is a single UV pair, 1.5 bytes for
//      each pixel or 36 bytes for 24 pixels
void yuv2rgb(int y, int u, int v, unsigned char* r, unsigned char* g,
             unsigned char* b) {

    // replaces floating point coefficients
    int const c = y - 16;
    int const d = u - 128;
    int const e = v - 128;

    // Conversion that avoids floating point
    int r1 = (298 * c + 409 * e + 128) >> 8;
    int g1 = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int b1 = (298 * c + 516 * d + 128) >> 8;

    // Computed values may need clipping.
    if (r1 > 255) {
        r1 = 255;
    }
    if (g1 > 255) {
        g1 = 255;
    }
    if (b1 > 255) {
        b1 = 255;
    }

    if (r1 < 0) {
        r1 = 0;
    }
    if (g1 < 0) {
        g1 = 0;
    }
    if (b1 < 0) {
        b1 = 0;
    }

    *r = r1;
    *g = g1;
    *b = b1;
}

void processImage(const void* const p, size_t size, const int frameCount,
                  const v4l2_format* const fmt) {
    struct timespec frameTime;
    const unsigned char* pptr = (unsigned char*)p;

    // record when process was called
    clock_gettime(CLOCK_REALTIME, &frameTime);

    printf("frame %d: ", frameCount);

    // This just dumps the frame to a file now, but you could replace with
    // whatever image processing you wish.
    //

    if (fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_GREY) {
        printf("Dump graymap as-is size %zu\n", size);
        dumpPgm(p, size, frameCount, &frameTime);
    } else if (fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
        printf("Dump YUYV converted to RGB size %zu\n", size);

        unsigned char bigbuffer[(1280 * 960)];

        // Pixels are YU and YV alternating, so YUYV which is 4 bytes
        // We want RGB, so RGBRGB which is 6 bytes
        //
        for (int i = 0, newi = 0; i < size; i = i + 4, newi = newi + 6) {
            int const yTemp = (int)pptr[i];
            int const uTemp = (int)pptr[i + 1];
            int const y2Temp = (int)pptr[i + 2];
            int const vTemp = (int)pptr[i + 3];

            yuv2rgb(yTemp, uTemp, vTemp, &bigbuffer[newi], &bigbuffer[newi + 1],
                    &bigbuffer[newi + 2]);
            yuv2rgb(y2Temp, uTemp, vTemp, &bigbuffer[newi + 3],
                    &bigbuffer[newi + 4], &bigbuffer[newi + 5]);
        }

        dumpPpm(bigbuffer, (size * 6) / 4, frameCount, &frameTime);

    } else if (fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_RGB24) {
        printf("Dump RGB as-is size %zu\n", size);
        dumpPpm(p, size, frameCount, &frameTime);
    } else {
        printf("ERROR - unknown dump format\n");
    }

    fflush(stderr);
    // fprintf(stderr, ".");
    fflush(stdout);
}

int readFrame(mqd_t messageQueue, const std::vector<Buffer>& buffers,
              int fileDescriptor) {
    v4l2_buffer buf;

    std::memset(&buf, 0, sizeof(buf));

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fileDescriptor, VIDIOC_DQBUF, &buf)) {
        switch (errno) {
            case EAGAIN:
                return 0;

            case EIO:
                /* Could ignore EIO, but drivers should only set for serious errors,
 although some set for non-fatal errors too.
*/
                return 0;

            default:
                printf("mmap failure\n");
                errnoExit("VIDIOC_DQBUF");
        }
    }

    assert(buf.index < buffers.size());
    Buffer buffer = {
        malloc(buf.bytesused),
        buf.bytesused,
    };

    memcpy(buffer.start, buffers[buf.index].start, buf.bytesused);
    syslog(LOG_INFO, "Reading Image");
    mq_send(messageQueue, (char*)&buffer, sizeof(Buffer), 0);

    if (-1 == xioctl(fileDescriptor, VIDIOC_QBUF, &buf)) {
        errnoExit("VIDIOC_QBUF");
    }

    return 1;
}

void* processImages(void* arg) {
    const ProcessImagesArgs* const args = (ProcessImagesArgs*)arg;

    for (int i = 0; i < args->numFrames; ++i) {
        Buffer buffer;
        if (-1 == mq_receive(args->messageQueue, (char*)&buffer, sizeof(buffer),
                             NULL)) {
            perror("mq_receive");
            return NULL;
        }

        syslog(LOG_INFO, "Processing Image");

        processImage(buffer.start, buffer.length, i, args->format);

        free(buffer.start);
    }

    return NULL;
}

void stopCapturing(int fd) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == util::xioctl(fd, VIDIOC_STREAMOFF, &type)) {
        util::errnoExit("VIDIOC_STREAMOFF");
    }
}

void startCapturing(int fd, unsigned int numBuffers) {
    unsigned int i;
    enum v4l2_buf_type type;

    for (i = 0; i < numBuffers; ++i) {
        printf("allocated buffer %d\n", i);
        struct v4l2_buffer buf;

        std::memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (-1 == util::xioctl(fd, VIDIOC_QBUF, &buf)) {
            util::errnoExit("VIDIOC_QBUF");
        }
    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == util::xioctl(fd, VIDIOC_STREAMON, &type)) {
        util::errnoExit("VIDIOC_STREAMON");
    }
}

void uninitDevice(std::vector<Buffer>* const buffers) {
    unsigned int i;

    for (i = 0; i < buffers->size(); ++i) {
        if (-1 == munmap((*buffers)[i].start, (*buffers)[i].length)) {
            util::errnoExit("munmap");
        }
    }
}

void initMmap(int fd, const std::string& devName,
              std::vector<Buffer>* buffers) {
    struct v4l2_requestbuffers req;

    std::memset(&req, 0, sizeof(req));

    req.count = 6;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == util::xioctl(fd, VIDIOC_REQBUFS, &req)) {
        if (EINVAL == errno) {
            fprintf(stderr,
                    "%s does not support "
                    "memory mapping\n",
                    devName.c_str());
            exit(EXIT_FAILURE);
        } else {
            util::errnoExit("VIDIOC_REQBUFS");
        }
    }

    if (req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory on %s\n", devName.c_str());
        exit(EXIT_FAILURE);
    }

    buffers->resize(req.count);

    for (size_t i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf;

        std::memset(&buf, 0, sizeof(buf));

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (-1 == util::xioctl(fd, VIDIOC_QUERYBUF, &buf)) {
            util::errnoExit("VIDIOC_QUERYBUF");
        }

        (*buffers)[i].length = buf.length;
        (*buffers)[i].start =
            mmap(NULL /* start anywhere */, buf.length,
                 PROT_READ | PROT_WRITE /* required */,
                 MAP_SHARED /* recommended */, fd, buf.m.offset);

        if (MAP_FAILED == (*buffers)[i].start) {
            util::errnoExit("mmap");
        }
    }
}

void initDevice(int fd, const std::string& devName, v4l2_format* const format,
                std::vector<Buffer>* buffers) {
    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    unsigned int min;

    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n", devName.c_str());
            exit(EXIT_FAILURE);
        }

        errnoExit("VIDIOC_QUERYCAP");
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "%s is no video capture device\n", devName.c_str());
        exit(EXIT_FAILURE);
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "%s does not support streaming i/o\n", devName.c_str());
        exit(EXIT_FAILURE);
    }

    /* Select video input, video standard and tune here. */

    std::memset(&cropcap, 0, sizeof(cropcap));

    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect; /* reset to default */

        if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
            switch (errno) {
                case EINVAL:
                    /* Cropping not supported. */
                    break;
                default:
                    /* Errors ignored. */
                    break;
            }
        }
    } else {
        /* Errors ignored. */
    }

    std::memset(format, 0, sizeof(*format));

    format->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    printf("FORCING FORMAT\n");
    format->fmt.pix.width = 320;
    format->fmt.pix.height = 240;

    // Specify the Pixel Coding Formate here

    // This one work for Logitech C200
    format->fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

    // fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
    // fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_VYUY;

    // Would be nice if camera supported
    // fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
    // fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;

    // fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
    format->fmt.pix.field = V4L2_FIELD_NONE;

    if (-1 == xioctl(fd, VIDIOC_S_FMT, format)) {
        errnoExit("VIDIOC_S_FMT");
    }

    /* Buggy driver paranoia. */
    min = format->fmt.pix.width * 2;
    if (format->fmt.pix.bytesperline < min) {
        format->fmt.pix.bytesperline = min;
    }
    min = format->fmt.pix.bytesperline * format->fmt.pix.height;
    if (format->fmt.pix.sizeimage < min) {
        format->fmt.pix.sizeimage = min;
    }

    initMmap(fd, devName, buffers);
}

void closeDevice(int fd) {
    if (-1 == close(fd)) {
        errnoExit("close");
    }

    fd = -1;
}

void openDevice(int* const fd, const std::string& devName) {
    struct stat st;

    if (-1 == stat(devName.c_str(), &st)) {
        fprintf(stderr, "Cannot identify '%s': %d, %s\n", devName.c_str(),
                errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (!S_ISCHR(st.st_mode)) {
        fprintf(stderr, "%s is no device\n", devName.c_str());
        exit(EXIT_FAILURE);
    }

    *fd = open(devName.c_str(), O_RDWR /* required */ | O_NONBLOCK, 0);

    if (-1 == *fd) {
        fprintf(stderr, "Cannot open '%s': %d, %s\n", devName.c_str(), errno,
                strerror(errno));
        exit(EXIT_FAILURE);
    }
}

};  // namespace util
