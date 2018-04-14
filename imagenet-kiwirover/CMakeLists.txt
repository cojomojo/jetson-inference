
file(GLOB imagenetCameraSources *.cpp)
file(GLOB imagenetCameraIncludes *.h )

cuda_add_executable(imagenet-kiwirover ${imagenetCameraSources})
target_link_libraries(imagenet-kiwirover nvcaffe_parser nvinfer jetson-inference)
