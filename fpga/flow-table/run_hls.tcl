open_project project
set_top flowTableHandler
#add_files -tb flowTable_tb.cpp -cflags "-I../common"
add_files flowTable.cpp -cflags "-I../common"
open_solution -reset solution
set_part XC7V2000T
create_clock -period 6.3
config_rtl -reset_level low
config_dataflow -default_channel fifo -fifo_depth 32
config_compile -name_max_length 10000
csynth_design
#cosim_design
#export_design
exit
