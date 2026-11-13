file(GLOB MP_Pipeline_Sources RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  "*.cpp"
  "*.h"

  "avs/*.h"
)

# the slave process: its main and the sources it shares with the plugin
set(MP_Pipeline_Slave_Sources
  "slave_main.cpp"
  "statement.cpp"
  "trace.cpp"
)
list(REMOVE_ITEM MP_Pipeline_Sources "slave_main.cpp")
