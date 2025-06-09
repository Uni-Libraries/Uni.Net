#
# Library
#

add_library(uni.net STATIC)



#
# Sources
#

file(GLOB_RECURSE src CONFIGURE_DEPENDS
    "*.c"
)

target_sources(uni.net PRIVATE ${src})



#
# Properties
#

#language standard)
if(MSVC)
    target_compile_features(uni.net PUBLIC c_std_17)
else()
    target_compile_features(uni.net PUBLIC c_std_23)
endif()


#
# Includes
#

target_include_directories(uni.net PUBLIC ".")



#
# Libraries
#

target_link_libraries(uni.net PUBLIC freertos_kernel)
target_link_libraries(uni.net PUBLIC freertos_tcp)
target_link_libraries(uni.net PUBLIC uni.hal)
