QT += widgets
CONFIG += c++11
TARGET = screenshot
INCLUDEPATH += ..
SOURCES += screenshot.cpp ../mainwindow.cpp ../mainwindow_dos.cpp ../mainwindow_dfs.cpp ../viewer.cpp ../viewtext.cpp \
           ../dosfs.cpp ../dfs.cpp ../diskformat.cpp ../adfs.cpp ../newdisc.cpp ../dragout.cpp
HEADERS += ../dragout.h ../mainwindow.h ../mainwindow_p.h ../viewer.h ../viewtext.h ../dosfs.h ../dfs.h ../diskformat.h ../adfs.h
