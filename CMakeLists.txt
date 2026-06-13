cmake_minimum_required(VERSION 3.20)
project(freeflume
    VERSION 1.0.2
    DESCRIPTION "Native desktop streaming front-end (FreeFlume, desktop port)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Network Sql DBus)

# libmpv via pkg-config.
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
set(MPV_LINK PkgConfig::MPV)

add_executable(freeflume
    src/main.cpp
    src/depcheck.cpp
    src/mainwindow.cpp
    src/extractor.cpp
    src/searchpage.cpp
    src/mpvwidget.cpp
    src/idleinhibitor.cpp
    src/playerpage.cpp
    src/seekslider.cpp
    src/thumbnailloader.cpp
    src/detailpane.cpp
    src/database.cpp
    src/metaenricher.cpp
    src/videolist.cpp
    src/historypage.cpp
    src/subscriptionspage.cpp
    src/subscriptionfeed.cpp
    src/playlistspage.cpp
    src/settingspage.cpp
    src/theme.cpp
    src/sponsorblock.cpp
    src/downloadmanager.cpp
    src/downloadspage.cpp
    src/shortcuts.cpp
    src/reorderlistwidget.h  # header-only QObject; listed so AUTOMOC processes it
    packaging/icons/freeflume.qrc
)

target_link_libraries(freeflume PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Network
    Qt6::Sql
    Qt6::DBus
    ${MPV_LINK}
)
target_compile_options(freeflume PRIVATE -Wall -Wextra)
target_compile_definitions(freeflume PRIVATE FREEFLUME_VERSION="${PROJECT_VERSION}")

install(TARGETS freeflume RUNTIME DESTINATION bin)
install(FILES packaging/org.freeflume.Desktop.desktop
        DESTINATION share/applications)

# Icons into the hicolor theme so the .desktop entry resolves Icon=freeflume.
install(FILES packaging/icons/freeflume.svg
        DESTINATION share/icons/hicolor/scalable/apps RENAME freeflume.svg)
foreach(sz 16 24 32 48 64 128 256 512)
    install(FILES packaging/icons/freeflume-${sz}.png
            DESTINATION share/icons/hicolor/${sz}x${sz}/apps RENAME freeflume.png)
endforeach()
