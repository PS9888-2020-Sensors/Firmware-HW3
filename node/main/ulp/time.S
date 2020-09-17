#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"

#define IO_NUM 3

    .text
    .global entry
entry:
    wake
    halt