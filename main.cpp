#include <iostream>

#include <QImage>
#include <thread>
#include <fstream>

#include "v4l2encoder.h"

using namespace std;

void RGB2Yuv420p(unsigned char *yuv,
                               unsigned char *rgb,
                               int width,
                               int height)
{
  const size_t image_size = width * height;
  unsigned char *dst_y = yuv;
  unsigned char *dst_u = yuv + image_size;
  unsigned char *dst_v = yuv + image_size * 5 / 4;

    // Y plane
    for(size_t i = 0; i < image_size; i++)
    {
        int r = rgb[3 * i];
        int g = rgb[3 * i + 1];
        int b = rgb[3 * i + 2];
        *dst_y++ = ((67316 * r + 132154 * g + 25666 * b) >> 18 ) + 16;
    }

    // U and V plane
    for(size_t y = 0; y < height; y+=2)
    {
        for(size_t x = 0; x < width; x+=2)
        {
            const size_t i = y * width + x;
            int r = rgb[3 * i];
            int g = rgb[3 * i + 1];
            int b = rgb[3 * i + 2];
            *dst_u++ = ((-38856 * r - 76282 * g + 115138 * b ) >> 18 ) + 128;
            *dst_v++ = ((115138 * r - 96414 * g - 18724 * b) >> 18 ) + 128;
        }
    }
}

int main(int argc, char *argv[])
{
    v4l2Encoder enc;
    QImage image;
    image.load("test.bmp");
    image = image.convertToFormat(QImage::Format_RGB888);

    userbuffer data, yuv;

    std::fstream fs;
    fs.open("tmp.h264", std::ios_base::binary | std::ios_base::out);

    yuv.resize((image.width() * image.height() * 3)/2);
    while(1){
        RGB2Yuv420p(yuv.data(), image.bits(), image.width(), image.height());
        if(enc.encodeFrame((uint8_t*)yuv.data(), image.width(), image.height(), data)){
            if(!data.empty()){
                if(fs.is_open()){
                    fs.write((char*)data.data(), data.size());
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(32));
    }

    fs.close();;

    cout << "Hello World!" << endl;
    return 0;
}
