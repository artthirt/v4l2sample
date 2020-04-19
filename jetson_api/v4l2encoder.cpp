#include "v4l2encoder.h"

#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>
#include <v4l2_nv_extensions.h>
#include <nvbuf_utils.h>

#include <string>

#include "nvvideoencoder.h"

//#include <NvVideoEncoder.h>

#define CHECK(val) if(val < 0){ printf("error num %d", val); return false; }

class v4l2EncoderPrivate{
public:
    bool mInit = false;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    uint32_t mBitrate = 10e6;
    uint32_t mFrameRate = 60;
    uint32_t mNumFrame = 0;
    uint32_t mIFrameInterval = 1;
    uint32_t mNumBFrames = 0;
    uint32_t mNumCaptureBuffers = 4;
    uint32_t mNumOutputBuffers = 4;

    std::shared_ptr<NvVideoEncoder> mNVEncoder;

    v4l2EncoderPrivate(){

    }
    ~v4l2EncoderPrivate(){
        release();
    }

    bool init(uint32_t width, uint32_t height){
        mWidth = width;
        mHeight = height;
        mInit = false;
        int ret = 0;

        mNVEncoder.reset(NvVideoEncoder::createVideoEncoder("enc0"));

        uint32_t sz = mWidth * mHeight + mWidth * mHeight/2;

        ret = mNVEncoder->setBitrate(mBitrate);
        CHECK(ret);
        ret = mNVEncoder->setCapturePlaneFormat(V4L2_PIX_FMT_H264, mWidth, mHeight, sz);
        CHECK(ret);
        ret = mNVEncoder->setOutputPlaneFormat(V4L2_PIX_FMT_YUV420M, mWidth, mHeight);
        CHECK(ret);
        ret = mNVEncoder->setFrameRate(mFrameRate, 1);
        CHECK(ret);
        ret = mNVEncoder->setNumBFrames(mNumBFrames);
        CHECK(ret);
        ret = mNVEncoder->setIFrameInterval(mIFrameInterval);
        CHECK(ret);
        ret = mNVEncoder->setProfile(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);
        CHECK(ret);
        ret = mNVEncoder->setLevel(V4L2_MPEG_VIDEO_H264_LEVEL_5_0);
        CHECK(ret);
        ret = mNVEncoder->output_plane.setupPlane(V4L2_MEMORY_MMAP, mNumOutputBuffers, true, false);
        CHECK(ret);
        ret = mNVEncoder->capture_plane.setupPlane(V4L2_MEMORY_MMAP, mNumCaptureBuffers, true, false);
        CHECK(ret);
        ret = mNVEncoder->output_plane.setStreamStatus(true);
        CHECK(ret);
        ret = mNVEncoder->capture_plane.setStreamStatus(true);
        CHECK(ret);

        mInit = true;

        for(uint32_t i = 0; i < mNVEncoder->capture_plane.getNumBuffers(); ++i){
            struct v4l2_buffer v4l2_buf;
            struct v4l2_plane planes[MAX_PLANES];

            memset(&v4l2_buf, 0, sizeof(v4l2_buf));
            memset(planes, 0, MAX_PLANES * sizeof(struct v4l2_plane));

            v4l2_buf.index = i;
            v4l2_buf.m.planes = planes;

            ret = mNVEncoder->capture_plane.qBuffer(v4l2_buf);
            if (ret < 0)
            {
                return false;
            }
        }

        return true;
    }

    void release(){
        if(mNVEncoder){
            mNVEncoder->output_plane.setStreamStatus(false);
            mNVEncoder->capture_plane.setStreamStatus(false);
            mNVEncoder->capture_plane.waitForDQThread(2000);
        }
        mNVEncoder.reset();
    }

    template< typename F >
    int set_plane_dq(NvPlane *plane, F fun){
        int ret;
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];
        mapbuffer *buffer = nullptr;
        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, sizeof(planes));

        v4l2_buf.m.planes = planes;
        v4l2_buf.length = plane->n_planes;

        if(mNumFrame < plane->getNumBuffers()){
            buffer = plane->getNthBuffer(mNumFrame);
            v4l2_buf.index = mNumFrame;
        }else{
            ret = plane->dqBuffer(v4l2_buf, &buffer);
            if(ret){
                return ret;
            }
        }

        fun(buffer);

        int fd = buffer->planes[0].fd;
        for (uint32_t j = 0 ; j < buffer->n_planes ; j++){
            void** dat = (void **)&buffer->planes[j].buf;
            ret = NvBufferMemSyncForDevice (fd, j, dat);
            if (ret < 0){
                return ret;
            }
        }

        ret = plane->qBuffer(v4l2_buf);
        return ret;
    }

    template< typename F >
    int capture_plane_dq(NvPlane *plane, F fun){
        int ret;
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];
        mapbuffer *buffer = nullptr;

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, sizeof(planes));

        v4l2_buf.m.planes = planes;
        v4l2_buf.length = plane->n_planes;

        ret = plane->dqBuffer(v4l2_buf, &buffer);
        if(ret){
            return ret;
        }

        fun(buffer);

        ret = plane->qBuffer(v4l2_buf);
        return ret;
    }


    bool encode(uint8_t* data, uint32_t width, uint32_t height, userbuffer& output, bool nv12){
        if(!width || !height){
            return false;
        }
        if(!mInit || width != mWidth || height != mHeight){
            release();
            init(width, height);
        }
        if(!mInit){
            return false;
        }

        int ret = 0;

        std::cout << "output plane\n";
        int ret1 = set_plane_dq(&mNVEncoder->output_plane, [this, data, nv12](mapbuffer* buffer){
            copyframe(buffer, data, nv12);
        });

        mNumFrame++;

        if(ret1 != 0){
            return -1;
        }
        std::cout << "capture plane\n";
        ret1 = capture_plane_dq(&mNVEncoder->capture_plane,

        [&](mapbuffer* buffer)
        {
            Buffer &b = buffer->planes[0];
            output.resize(b.bytesused);
            std::copy(b.buf, b.buf + b.bytesused, output.begin());
            std::cout << "bytes write " << b.bytesused << "; numframe " << mNumFrame << std::endl;
            size_t s = std::min((size_t)10, output.size());
            for(int i = 0; i < s; ++i){
                std::cout << (ushort) output[i] << " ";
            }
            std::cout << "\n";
        }
        );

        return ret == 0;
    }

    int copyframe(mapbuffer* buffer, uint8_t *data, bool nv12){
        if(buffer == nullptr || data == nullptr)
            return -1;

        if(mNVEncoder->pixFmt() == V4L2_PIX_FMT_YUV420M){
            if(nv12){
                uint8_t *datas[3] = {buffer->planes[0].buf, buffer->planes[1].buf, buffer->planes[2].buf};
                uint32_t lines[3] = {buffer->planes[0].fmt.bytesperline, buffer->planes[1].fmt.bytesperline, buffer->planes[2].fmt.bytesperline};

                uint32_t sz = mWidth * mHeight;
                uint8_t *y = data;
                uint8_t *uv = data + sz;
                uint32_t bpy0 = mWidth;
                uint32_t bpuv0 = mWidth;
                uint32_t bpy1 = lines[0];
                uint32_t bpuv1 = lines[1];

                for(int i = 0; i < mHeight; ++i){
                    std::copy(y, y + bpy0, datas[0] + i * bpy1);
                    y += bpy0;
                }
                for(int i = 0; i < mHeight/2; ++i){
                    for(int j = 0; j < mWidth; ++j){
//                      std::copy(u, u + bpuv0, datas[1] + i * bpuv1);
//                      std::copy(v, v + bpuv0, datas[2] + i * bpuv1);
                        ushort *duv = (ushort*)(uv + j * 2);

                        uint8_t *du = (uint8_t*)(datas[1] + i * bpuv1 + j);
                        uint8_t *dv = (uint8_t*)(datas[2] + i * bpuv1 + j);

                        du[0] = duv[0] & 0xF;
                        dv[0] = (duv[0] >> 8);
                    }
                    uv += bpuv0;
                }

                buffer->planes[0].bytesused = buffer->planes[0].fmt.sizeimage;
                buffer->planes[1].bytesused = buffer->planes[1].fmt.sizeimage;
                buffer->planes[2].bytesused = buffer->planes[2].fmt.sizeimage;

            }else{
                uint8_t *datas[3] = {buffer->planes[0].buf, buffer->planes[1].buf, buffer->planes[2].buf};
                uint32_t lines[3] = {buffer->planes[0].fmt.bytesperline, buffer->planes[1].fmt.bytesperline, buffer->planes[2].fmt.bytesperline};

                uint32_t sz = mWidth * mHeight;
                uint8_t *y = data;
                uint8_t *u = data + sz;
                uint8_t *v = data + sz + sz/4;
                uint32_t bpy0 = mWidth;
                uint32_t bpuv0 = mWidth/2;
                uint32_t bpy1 = lines[0];
                uint32_t bpuv1 = lines[1];

                for(int i = 0; i < mHeight; ++i){
                    std::copy(y, y + bpy0, datas[0] + i * bpy1);
                    y += bpy0;
                }
                for(int i = 0; i < mHeight/2; ++i){
                    std::copy(u, u + bpuv0, datas[1] + i * bpuv1);
                    std::copy(v, v + bpuv0, datas[2] + i * bpuv1);
                    u += bpuv0;
                    v += bpuv0;
                }

                buffer->planes[0].bytesused = buffer->planes[0].fmt.sizeimage;
                buffer->planes[1].bytesused = buffer->planes[1].fmt.sizeimage;
                buffer->planes[2].bytesused = buffer->planes[2].fmt.sizeimage;
            }
        }

        return 0;
    }
};

///////////////////////////////////
///////////////////////////////////

v4l2Encoder::v4l2Encoder()
{
    mD.reset(new v4l2EncoderPrivate);
}

v4l2Encoder::~v4l2Encoder()
{
    mD.reset();
}

void v4l2Encoder::setIFrameInterval(int val)
{
    mD->mIFrameInterval = val;
}

void v4l2Encoder::setNumBFrames(int val)
{
    mD->mNumBFrames = val;
}

void v4l2Encoder::setFrameRate(int fps)
{
    mD->mFrameRate = fps;
}

void v4l2Encoder::setBitrate(int bitrate)
{
    mD->mBitrate = bitrate;
}

void v4l2Encoder::setNumCaptureBuffers(int val)
{
    mD->mNumCaptureBuffers = val;
}

void v4l2Encoder::setNumOutputBuffers(int val)
{
    mD->mNumOutputBuffers = val;
}

bool v4l2Encoder::encodeFrame(uint8_t *buf, int width, int height, userbuffer& output, bool nv12)
{
    return mD->encode(buf, width, height, output, nv12);
}
