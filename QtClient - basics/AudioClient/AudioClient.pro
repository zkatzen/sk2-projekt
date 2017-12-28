#-------------------------------------------------
#
# Project created by QtCreator 2017-12-13T00:01:04
#
#-------------------------------------------------

QT += core gui
QT += network
QT += multimedia
QT += widgets
CONFIG += c++11

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = AudioClient
TEMPLATE = app


SOURCES += main.cpp\
        clientwindow.cpp

HEADERS  += clientwindow.h

FORMS    += clientwindow.ui
