#include <iostream>

#include <QImage>
#include <QPen>
#include <QPainter>
#include <QDateTime>
#include <thread>
#include <fstream>
#include <iostream>
#include <cmath>

#include "v4l2encoder.h"

using namespace std;


void drawNumbers(QImage& image, int x, int y, const QString& lines)
{

}

QImage drawTime(QImage &im, const QDateTime& dt)
{
    QImage out = im.copy();
    QPainter painter(&out);

    QString tm = dt.toString("hh:mm:ss.zzz");

    qint64 t = dt.toMSecsSinceEpoch();
    double d = sin(0.01 * t);
    int c = 128 + 127 * d;
    QColor color(c, c, c);

    painter.fillRect(QRect(10, 10, 100, 100), color);
//    QPen pen;
//    pen.setWidth(2);
//    pen.setColor(Qt::white);

//    QFont f = painter.font();
//    f.setPointSize(50);

//    painter.setFont(f);
//    painter.setPen(pen);

//    painter.drawLine(20, 100, 20, 200);

    return out;
}

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

    int countFrames = 10000;

    yuv.resize((image.width() * image.height() * 3)/2);
    while(countFrames-- >= 0){
        std::cout << "count frames " << countFrames << std::endl;
        QImage out = drawTime(image, QDateTime::currentDateTime());
        RGB2Yuv420p(yuv.data(), out.bits(), out.width(), out.height());
        if(enc.encodeFrame((uint8_t*)yuv.data(), out.width(), out.height(), data)){
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
