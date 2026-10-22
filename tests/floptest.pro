QT = core
CONFIG += c++11 console
TARGET = floptest
INCLUDEPATH += ..
SOURCES += floptest.cpp ../adfs.cpp ../dosfs.cpp ../dfs.cpp ../diskformat.cpp
HEADERS += ../adfs.h ../dosfs.h ../dfs.h ../diskformat.h
