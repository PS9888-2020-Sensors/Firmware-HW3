# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES fatfs common mtftp)

set(COMPONENT_SRCS "main.cpp" "mtftp_task.cpp" "time_sync_task.cpp")
set(COMPONENT_ADD_INCLUDEDIRS "")

register_component()
