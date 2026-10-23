cmake_minimum_required(VERSION 3.5)
project(acorndiskwrangler LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt5 REQUIRED COMPONENTS Widgets)
include(GNUInstallDirs)
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
add_executable(acorndiskwrangler MACOSX_BUNDLE ${ACORNDISKWRANGLER_RC} ${ACORNDISKWRANGLER_ICNS} main.cpp mainwindow.cpp mainwindow_dos.cpp mainwindow_dfs.cpp dfs.cpp diskformat.cpp viewer.cpp viewtext.cpp dosfs.cpp adfs.cpp icon.cpp newdisc.cpp dragout.cpp
    icon.h mainwindow.h mainwindow_p.h viewer.h viewtext.h dosfs.h dfs.h diskformat.h adfs.h newdisc.h dragout.h)
target_link_libraries(acorndiskwrangler Qt5::Widgets)
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
