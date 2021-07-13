
.global start_sigret
.global end_sigret

start_sigret:
    li a7,24;
    ecall;
    ret;
end_sigret:
