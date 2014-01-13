message("Running qmake...")


TEMPLATE = app
TARGET = QtWeb
DESTDIR = ./release
QT += network xml webkit
!os2 {
CONFIG += static
QTPLUGIN += qcncodecs qjpcodecs qkrcodecs qtwcodecs  qgif qjpeg qico 
}
DEFINES += QT_NO_UITOOLS

INCLUDEPATH += ./tmp/moc/release_static \
    . \
    ./tmp/moc/Release_static \
    ./tmp/rcc/Release_static

DEPENDPATH += .
MOC_DIR += ./tmp/moc/release_static
OBJECTS_DIR += release
UI_DIR += .
RCC_DIR += ./tmp/rcc/release_static

#Include file(s)
include(QtWeb.pri)

#Windows resource file
win32:RC_FILE = QtWeb.rc

#eComStation (OS/2) resource file
os2:RC_FILE = QtWeb_os2.rc

macx:ICON = qtweb.icns

message("qmake finished.")
