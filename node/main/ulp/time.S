#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"

#define IO_NUM 3

    .text
    .global entry
entry:
    wake
    halt

read_io:
    // assume io_num is in the lower 16 RTC io pins
    // read value at io pin
    READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S, 16)

    // get bit val and mask everything else off
    // and r0, r0, IO_NUM should do the same but it dosent seem to work?
    rsh r0, r0, IO_NUM
    and r0, r0, 1

    // if r0 is HIGH (not asserted), skip
    jumpr read_io, 0, gt

    wake
    jump read_io