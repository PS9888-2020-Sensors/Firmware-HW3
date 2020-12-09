# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES common mtftp)

set(COMPONENT_SRCS "dump_data.cpp" "main.cpp" "mtftp_task.cpp" "write_task.cpp")
set(COMPONENT_ADD_INCLUDEDIRS "")

register_component()
