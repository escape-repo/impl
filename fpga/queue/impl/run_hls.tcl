open_project project
set_top queueHandler
#add_files -tb queue_tb.cpp -cflags "-I../../common"
add_files queue.cpp -cflags "-I../../common"
open_solution -reset solution
set_part XC7V2000T
create_clock -period 6.3
config_rtl -reset_level low
config_dataflow -default_channel fifo -fifo_depth 32
config_compile -name_max_length 10000
#csim_design -clean -argv {../csim/input.dat}
csynth_design
#cosim_design -rtl verilog -trace_level none -argv {../csim/input.dat}
#export_design
exit
