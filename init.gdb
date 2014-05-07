# set kernel memcheck on
handle SIGSEGV nostop noprint nopass
break dbg_panic_halt
break hard_shutdown
break bootstrap
add-symbol-file user/usr/bin/memtest.exec 0x08048540
continue
