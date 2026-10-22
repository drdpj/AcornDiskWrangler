QT += widgets
CONFIG += c++11
TARGET = guitest2
INCLUDEPATH += ..
SOURCES += guitest2.cpp ../mainwindow.cpp ../mainwindow_dos.cpp ../mainwindow_dfs.cpp ../viewer.cpp ../viewtext.cpp \
           ../dosfs.cpp ../dfs.cpp ../diskformat.cpp ../adfs.cpp ../newdisc.cpp
HEADERS += ../mainwindow.h ../mainwindow_p.h ../viewer.h ../viewtext.h ../dosfs.h ../dfs.h ../diskformat.h ../adfs.h
