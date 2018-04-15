
file(GLOB imagenetCameraSources *.cpp)
file(GLOB imagenetCameraIncludes *.h )
file(COPY "config.xml" DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

cuda_add_executable(imagenet-kiwirover ${imagenetCameraSources})
target_link_libraries(imagenet-kiwirover nvcaffe_parser nvinfer jetson-inference)
