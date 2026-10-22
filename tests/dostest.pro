QT = core
CONFIG += c++11 console
TARGET = dostest
INCLUDEPATH += ..
SOURCES += dostest.cpp ../adfs.cpp ../dosfs.cpp
HEADERS += ../adfs.h ../dosfs.h
