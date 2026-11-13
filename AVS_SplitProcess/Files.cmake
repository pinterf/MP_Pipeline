file(GLOB MP_Pipeline_Sources RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  "*.cpp"
  "*.h"

  "avs/*.h"
)

# the platform layer of this system: one part for both, one for the slave program only
if(WIN32)
  set(MP_Pipeline_Platform_Sources "platform_win32.cpp")
  set(MP_Pipeline_Platform_Slave_Sources "platform_win32_slave.cpp")
else()
  set(MP_Pipeline_Platform_Sources "platform_posix.cpp")
  set(MP_Pipeline_Platform_Slave_Sources "platform_posix_slave.cpp")
endif()
list(REMOVE_ITEM MP_Pipeline_Sources "slave_main.cpp"
  "platform_win32.cpp" "platform_win32_slave.cpp" "platform_posix.cpp" "platform_posix_slave.cpp")
list(APPEND MP_Pipeline_Sources ${MP_Pipeline_Platform_Sources})

# the slave process: its main, its platform part and the sources it shares with the plugin
set(MP_Pipeline_Slave_Sources
  "slave_main.cpp"
  ${MP_Pipeline_Platform_Sources}
  ${MP_Pipeline_Platform_Slave_Sources}
  "statement.cpp"
  "trace.cpp"
)