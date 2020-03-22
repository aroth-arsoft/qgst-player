cmake_minimum_required(VERSION 3.5)

project(qgst LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# QtCreator supports the following variables for Android, which are identical to qmake Android variables.
# Check http://doc.qt.io/qt-5/deployment-android.html for more information.
# They need to be set before the find_package(Qt5 ...) call.

#if(ANDROID)
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
#    if (ANDROID_ABI STREQUAL "armeabi-v7a")
#        set(ANDROID_EXTRA_LIBS
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libcrypto.so
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libssl.so)
#    endif()
#endif()

find_package(Qt5 COMPONENTS Widgets OpenGL REQUIRED)

option(QGST_USE_QTGSTREAMER "Use QtGStreamer" ON)

if(QGST_USE_QTGSTREAMER)
    find_package(Qt5GStreamer)
    include_directories(${QTGSTREAMER_INCLUDE_DIR})
    add_definitions(-DQGST_USE_QTGSTREAMER=1)
else()
    # requires: sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLIB REQUIRED glib-2.0)
    set(GSTREAMER_MINIMUM_VERSION 1.0.5)
    set(GSTREAMER_ABI_VERSION "1.0")

    pkg_check_modules(GST1_TEST gstreamer-${GSTREAMER_ABI_VERSION})
    if ( GST1_TEST_FOUND AND NOT ${GST1_TEST_VERSION} VERSION_LESS ${GSTREAMER_MINIMUM_VERSION} )
        pkg_check_modules(GSTREAMER REQUIRED gstreamer-${GSTREAMER_ABI_VERSION})
        pkg_check_modules(GSTREAMER-APP REQUIRED gstreamer-app-${GSTREAMER_ABI_VERSION})
        pkg_check_modules(GSTREAMER-AUDIO REQUIRED gstreamer-audio-${GSTREAMER_ABI_VERSION})
        pkg_check_modules(GSTREAMER-VIDEO REQUIRED gstreamer-video-${GSTREAMER_ABI_VERSION})
        pkg_check_modules(GSTREAMER-PBUTILS REQUIRED gstreamer-pbutils-${GSTREAMER_ABI_VERSION})
        add_definitions(-DGST_API_VERSION_1=1)

        find_library(GSTREAMER_LIBRARY
                     NAMES gstreamer-${GSTREAMER_ABI_VERSION}
                     HINTS ${PKG_GSTREAMER_LIBRARY_DIRS} ${PKG_GSTREAMER_LIBDIR})

         find_library(GSTREAMER_VIDEO_LIBRARY
                         NAMES gstvideo-${GSTREAMER_ABI_VERSION}
                         HINTS ${PKG_GSTREAMER_LIBRARY_DIRS} ${PKG_GSTREAMER_LIBDIR})

         find_library(GSTREAMER_AUDIO_LIBRARY
                         NAMES gstaudio-${GSTREAMER_ABI_VERSION}
                         HINTS ${PKG_GSTREAMER_LIBRARY_DIRS} ${PKG_GSTREAMER_LIBDIR})

        include_directories(
          ${GSTREAMER_INCLUDE_DIRS}
          ${GSTREAMER-APP_INCLUDE_DIRS}
          ${GSTREAMER-AUDIO_INCLUDE_DIRS}
          ${GSTREAMER-PBUTILS_INCLUDE_DIRS}
          ${GSTREAMER-VIDEO_INCLUDE_DIRS}
        )

    endif()
endif()

add_executable(qgst
    main.cpp
    player.cpp
    player.h
    videowidget.cpp
    videowidget.h
    MainWindow.cpp
    MainWindow.h
    MainWindow.ui
    OpenNetworkDialog.cpp
    OpenNetworkDialog.h
    OpenNetworkDialog.ui
  )

target_link_libraries(qgst PRIVATE Qt5::Widgets Qt5::OpenGL)
if(QGST_USE_QTGSTREAMER)
    target_link_libraries(qgst PRIVATE Qt5GStreamer Qt5GStreamerUi )
else()
    target_link_libraries(qgst PRIVATE ${GSTREAMER_LIBRARIES} ${GSTREAMER_VIDEO_LIBRARY} ${GSTREAMER_AUDIO_LIBRARY})
endif()
