cmake_minimum_required(VERSION 3.5)
cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_STANDARD 17)

if(MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
endif(MSVC)

project(obs-studio-datapath)
set (obs-studio-datapath_VERSION_MAJOR 0)
set (obs-studio-datapath_VERSION_MINOR 1)

set(obs-studio_datapath_SOURCES
	VisionSource.cpp
	VisionPlugin.cpp
	)

set(obs-studio-source-path "") 
set(rgbeasy-path "C:\\Program Files (x86)\\Vision\\SDK\\RGBEASY")
  
include_directories ("${obs-studio-source-path}\\libobs")
link_directories("${obs-studio-source-path}\\build64\\libobs\\Debug")

include_directories("${rgbeasy-path}\\C\\INCLUDE")
link_directories("${rgbeasy-path}\\C\\LIB\\x64\\Debug")

add_library(obs-studio-datapath MODULE ${obs-studio_datapath_SOURCES})
target_link_libraries(obs-studio-datapath obs rgbeasy)
