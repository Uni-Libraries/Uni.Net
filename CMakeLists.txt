#
# Project
#

cmake_minimum_required(VERSION 3.29)
project(uni.net LANGUAGES C CXX ASM)



#
# FreeRTOS
#

add_subdirectory(3rdparty/freertos_tcp)



#
# Uni.NET
#

add_subdirectory(src)
