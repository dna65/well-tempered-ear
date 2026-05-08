cmake_minimum_required(VERSION 3.23)
project(well-tempered-ear)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)

set(CMAKE_CXX_FLAGS "-Wall -Werror")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
find_package(libusb REQUIRED)

add_executable(wte)
target_sources(wte
  PRIVATE
    src/app.cc src/events.cc src/game.cc src/main.cc src/midi.cc src/sound.cc
    src/stream.cc src/usb.cc
)

target_link_libraries(wte
  PRIVATE
    SDL3::SDL3
    libusb::usb-1.0
)
