include(../mumble.pri)
TEMPLATE = app
TARGET = actd
HEADERS = mainwindow.h \
		  newconfwizard.h \
		  announcement.h \
		  sessionenum.h \
		  debugbox.h

SOURCES = mainwindow.cpp \
		  newconfwizard.cpp \
		  announcement.cpp \
		  sessionenum.cpp \
		  debugbox.cpp \
		  main.cpp 

RESOURCES = actd.qrc
DIST *= actd.icns images/audio.png images/logo1.png images/priv.png images/watermark1.png images/watermark2.png images/video.png images/setting.png

QMAKE_LIBDIR *= /usr/local/lib
INCLUDEPATH *= /usr/local/include
LIBS *= -lccn -lssl -lcrypto
ICON = actd.icns
CONFIG += console 
QT += xml

system("(test -e ~/.actd/actd_private_key.pem) &&( test -e ~/.actd/actd_cert.pem) || ./actd_initkeystore.sh")
include(../../symbols.pri)