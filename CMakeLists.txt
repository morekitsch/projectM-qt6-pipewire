cmake_minimum_required(VERSION 3.21)
project(qt6mplayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(USE_PIPEWIRE "Enable PipeWire capture backend" ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets)
find_package(PkgConfig QUIET)

if(USE_PIPEWIRE AND PkgConfig_FOUND)
  pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
  if(NOT PIPEWIRE_FOUND)
    pkg_check_modules(PIPEWIRE IMPORTED_TARGET pipewire-0.3)
  endif()
endif()

if(PkgConfig_FOUND)
  pkg_check_modules(PROJECTM IMPORTED_TARGET projectM-4)
endif()

set(APP_SOURCES
  src/main.cpp
  src/MainWindow.cpp
  src/PresetLibraryModel.cpp
  src/PresetFilterProxyModel.cpp
  src/PlaylistModel.cpp
  src/SettingsManager.cpp
  src/ProjectMEngine.cpp
  src/VisualizerWidget.cpp
  src/audio/AudioSourceFactory.cpp
  src/audio/DummyAudioSource.cpp
  src/audio/PipeWireAudioSource.cpp
  src/widgets/RatingDelegate.cpp
)

set(APP_HEADERS
  src/MainWindow.h
  src/PresetLibraryModel.h
  src/PresetFilterProxyModel.h
  src/PresetMetadata.h
  src/PlaylistModel.h
  src/SettingsManager.h
  src/ProjectMEngine.h
  src/VisualizerWidget.h
  src/audio/AudioSource.h
  src/audio/AudioSourceFactory.h
  src/audio/DummyAudioSource.h
  src/audio/PipeWireAudioSource.h
  src/widgets/RatingDelegate.h
)

qt_add_executable(qt6mplayer
  ${APP_SOURCES}
  ${APP_HEADERS}
)

target_link_libraries(qt6mplayer
  PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::OpenGLWidgets
    Qt6::Widgets
)

if(PIPEWIRE_FOUND)
  target_link_libraries(qt6mplayer PRIVATE PkgConfig::PIPEWIRE)
  target_compile_definitions(qt6mplayer PRIVATE HAVE_PIPEWIRE=1)
  message(STATUS "PipeWire backend enabled (${PIPEWIRE_VERSION})")
elseif(USE_PIPEWIRE)
  message(WARNING "PipeWire dev package not found. Building with dummy audio fallback only.")
else()
  message(STATUS "PipeWire backend disabled (USE_PIPEWIRE=OFF)")
endif()

if(PROJECTM_FOUND)
  target_link_libraries(qt6mplayer PRIVATE PkgConfig::PROJECTM)
  target_compile_definitions(qt6mplayer PRIVATE HAVE_PROJECTM=1)
endif()

target_compile_definitions(qt6mplayer PRIVATE QT_NO_KEYWORDS)

install(TARGETS qt6mplayer RUNTIME DESTINATION bin)
