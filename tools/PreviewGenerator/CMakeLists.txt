cmake_minimum_required(VERSION 3.15)

project(PreviewGenerator LANGUAGES CXX)

add_executable(PreviewGenerator
    PreviewGenerator/PreviewGenerator.cpp
    PreviewGenerator/third_party/gif/gif.h
    PreviewGenerator/third_party/stb/stb_image.h
)

target_compile_features(PreviewGenerator PRIVATE cxx_std_11)

if(MSVC)
    target_compile_options(PreviewGenerator PRIVATE /W3 /permissive-)
endif()
