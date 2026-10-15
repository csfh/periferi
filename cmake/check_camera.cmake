# The camera engine must never stream or read video data. Its one door to the kernel is
# peri_cam_ioctl(), which checks an allowlist (tests/camera_test.c), so that must stay the ONLY place
# camera.c talks to a device: exactly one raw ioctl( call, and no read, write, mmap or sendmsg calls.
# Run by ctest: cmake -DFILE=<src/camera.c> -P check_camera.cmake
file(READ "${FILE}" text)
string(REGEX MATCHALL "(^|[^_A-Za-z0-9])ioctl\\(" ioctls "${text}")
list(LENGTH ioctls n_ioctl)
if(NOT n_ioctl EQUAL 1)
  message(FATAL_ERROR "camera.c has ${n_ioctl} raw ioctl( calls, expected exactly 1 (inside peri_cam_ioctl)")
endif()
foreach(call read write pread pwrite mmap readv writev send sendmsg recv)
  string(REGEX MATCH "(^|[^_A-Za-z0-9])${call}\\(" hit "${text}")
  if(hit)
    message(FATAL_ERROR "camera.c calls ${call}(: the camera engine must never read or write video data")
  endif()
endforeach()
