# set kernel memcheck on
handle SIGSEGV nostop noprint nopass
break dbg_panic_halt
break hard_shutdown
break bootstrap
add-symbol-file user/usr/bin/eatmem.exec 0x080483f0
continue
