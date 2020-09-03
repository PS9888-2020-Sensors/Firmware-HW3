# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES fatfs ulp common mtftp)

set(COMPONENT_SRCS "main.cpp" "mtftp_task.cpp" "time_sync_task.cpp" "sensor_task.cpp" "bme280/bme280.c" "sample.cpp")
set(COMPONENT_ADD_INCLUDEDIRS "")

register_component()

set (ulp_app_name ulp_${COMPONENT_NAME})
set(ulp_s_sources ulp/time.S)
set(ulp_exp_dep_srcs "sample.cpp")

ulp_embed_binary(${ulp_app_name} "${ulp_s_sources}" "${ulp_exp_dep_srcs}")