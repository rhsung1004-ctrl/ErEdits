cmake_minimum_required(VERSION 3.21)
project(ERKillCut VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt 6 (화면 + 미리보기 재생). 영상 분석·내보내기는 함께 넣는 ffmpeg.exe 로 처리
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets)

add_executable(ERKillCut WIN32
  src/main.cpp
  src/MainWindow.h      src/MainWindow.cpp
  src/HudKillWatcher.h  src/HudKillWatcher.cpp
  src/KillScanner.h     src/KillScanner.cpp
  src/Segments.h        src/Segments.cpp
  src/Exporter.h        src/Exporter.cpp
  src/RoiDialog.h       src/RoiDialog.cpp
  src/TimelineBar.h     src/TimelineBar.cpp
  src/Util.h            src/Util.cpp
)

target_link_libraries(ERKillCut PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets)

if(MSVC)
  target_compile_options(ERKillCut PRIVATE /utf-8 /W3)
  target_compile_definitions(ERKillCut PRIVATE NOMINMAX)
endif()

# 테스트용: 화면 없이 킬 찾기·구간·필터 만들기 검사 (cmake -DERKILLCUT_TESTS=ON)
option(ERKILLCUT_TESTS "명령줄 테스트 도구도 빌드" OFF)
if(ERKILLCUT_TESTS)
  add_executable(erkillcut_cli tests/cli.cpp
    src/HudKillWatcher.cpp src/KillScanner.cpp src/Segments.cpp src/Exporter.cpp src/Util.cpp)
  target_include_directories(erkillcut_cli PRIVATE src)
  target_link_libraries(erkillcut_cli PRIVATE Qt6::Core)
endif()
