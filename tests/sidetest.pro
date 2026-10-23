QT += widgets
CONFIG += c++11
TARGET = sidetest
INCLUDEPATH += ..
SOURCES += sidetest.cpp ../mainwindow.cpp ../mainwindow_dos.cpp ../mainwindow_dfs.cpp ../viewer.cpp ../viewtext.cpp \
           ../dosfs.cpp ../dfs.cpp ../diskformat.cpp ../adfs.cpp ../newdisc.cpp ../dragout.cpp
HEADERS += ../mainwindow.h ../mainwindow_p.h ../viewer.h ../viewtext.h ../dosfs.h ../dfs.h ../diskformat.h ../adfs.h ../newdisc.h ../dragout.h
