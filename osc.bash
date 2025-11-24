#!/bin/bash

# Assumes kernel has isolcpus=3 in the cmdline to avoid scheduling things on
# cpu3.  Run the python code on cpu2 s.t. we can set affinity 3 on the ADC
# sampler thread and give it maximum priority.

sudo taskset -c 2 python osc.py
