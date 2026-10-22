cmake_minimum_required(VERSION 3.5)
project(acorndiskwrangler LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt5 REQUIRED COMPONENTS Widgets)
set(ACORNDISKWRANGLER_RC "")
if(WIN32)                      # the .exe's own icon (Explorer / taskbar) comes from a resource file
    enable_language(RC)
    set(ACORNDISKWRANGLER_RC acorndiskwrangler.rc)
endif()
add_executable(acorndiskwrangler ${ACORNDISKWRANGLER_RC} main.cpp mainwindow.cpp mainwindow_dos.cpp mainwindow_dfs.cpp dfs.cpp diskformat.cpp viewer.cpp viewtext.cpp dosfs.cpp adfs.cpp icon.cpp newdisc.cpp
    icon.h mainwindow.h mainwindow_p.h viewer.h viewtext.h dosfs.h dfs.h diskformat.h adfs.h newdisc.h)
target_link_libraries(acorndiskwrangler Qt5::Widgets)
