# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES fatfs common mtftp)

set(COMPONENT_SRCS "main.cpp" "mtftp_task.cpp" "time_sync_task.cpp" "sensor_task.cpp" "bme280/bme280.c")
set(COMPONENT_ADD_INCLUDEDIRS "")

register_component()
