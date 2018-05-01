
file(GLOB pylonCameraSources *.cpp)
file(GLOB pylonCameraIncludes *.h)

add_executable(pylon-camera ${pylonCameraSources})
target_link_libraries(pylon-camera jetson-inference)
