# Acorn Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
cmake_minimum_required(VERSION 3.16)   # Qt 6 needs 3.16
project(acorndiskwrangler LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# Qt 6 if it is installed, otherwise Qt 5 (5.15); -DUSE_QT5=ON builds with Qt 5 even when Qt 6 is there.
option(USE_QT5 "Build with Qt 5 instead of Qt 6" OFF)
if(USE_QT5)
    find_package(QT NAMES Qt5 REQUIRED COMPONENTS Widgets)
else()
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
include(GNUInstallDirs)
include(version.cmake)         # APP_VERSION from git describe (Help > About)
set(ACORNDISKWRANGLER_RC "")
if(WIN32)                      # the .exe's own icon (Explorer / taskbar) comes from a resource file
    enable_language(RC)
    set(ACORNDISKWRANGLER_RC acorndiskwrangler.rc)
endif()
set(ACORNDISKWRANGLER_ICNS "")
if(APPLE)                      # the .app's icon (Finder, Dock before it starts)
    set(ACORNDISKWRANGLER_ICNS icons/acorndiskwrangler.icns)
    set_source_files_properties(${ACORNDISKWRANGLER_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()
add_executable(acorndiskwrangler MACOSX_BUNDLE ${ACORNDISKWRANGLER_RC} ${ACORNDISKWRANGLER_ICNS} main.cpp licences.cpp licences.h licences.qrc mainwindow.cpp mainwindow_dos.cpp mainwindow_dfs.cpp dfs.cpp diskformat.cpp viewer.cpp viewtext.cpp dosfs.cpp adfs.cpp icon.cpp newdisc.cpp dragout.cpp
    appsettings.h buttonbar.h fileopen.h icon.h mainwindow.h mainwindow_p.h viewer.h viewtext.h dosfs.h dfs.h diskformat.h adfs.h newdisc.h dragout.h)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/licences-bundled.qrc)   # a release build's library licences (ci/)
    target_sources(acorndiskwrangler PRIVATE licences-bundled.qrc)
endif()
target_compile_definitions(acorndiskwrangler PRIVATE APP_VERSION="${APP_VERSION}")
if(APP_VERSION_NUMBER)
    set_target_properties(acorndiskwrangler PROPERTIES MACOSX_BUNDLE_SHORT_VERSION_STRING ${APP_VERSION_NUMBER}
                                                 MACOSX_BUNDLE_BUNDLE_VERSION ${APP_VERSION_NUMBER})
endif()
target_link_libraries(acorndiskwrangler Qt${QT_VERSION_MAJOR}::Widgets)
set_target_properties(acorndiskwrangler PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Acorn Disk Wrangler"
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.drdpj.acorndiskwrangler
    MACOSX_BUNDLE_ICON_FILE acorndiskwrangler.icns)
install(TARGETS acorndiskwrangler RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION .)
if(UNIX AND NOT APPLE)         # where Linux docks and menus look for the name and icon (see acorndiskwrangler.desktop)
    install(FILES acorndiskwrangler.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES icons/acorndiskwrangler.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    foreach(s 16 24 32 48 64 128 256 512)
        install(FILES icons/acorndiskwrangler-${s}.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${s}x${s}/apps
                RENAME acorndiskwrangler.png)
    endforeach()
endif()
