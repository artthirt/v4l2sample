TEMPLATE = app
TARGET = nvsample
CONFIG += c++11

QT += core gui

SOURCES += main.cpp

OBJECTS_DIR = tmp/obj
MOC_DIR = tmp/moc
RCC_DIR = tmp/rcc
UI_DIR = tmp/ui

NVUTILSDIR=/usr/src/jetson_multimedia_api/include/

#INCLUDEPATH += $$NVUTILSDIR

TEGRA_ARMABI = aarch64-linux-gnu
# Location of the CUDA Toolkit
CUDA_PATH 	= /usr/local/cuda

LIBS += -lv4l2

include(jetson_api/jetson_api.pri)
