QT = core
CONFIG += c++11 console
TARGET = detecttest
INCLUDEPATH += ..
SOURCES += detecttest.cpp ../diskformat.cpp ../adfs.cpp ../dfs.cpp ../dosfs.cpp
HEADERS += ../diskformat.h ../adfs.h ../dfs.h ../dosfs.h
