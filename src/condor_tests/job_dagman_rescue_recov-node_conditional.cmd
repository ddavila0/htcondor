executable   = ./job_dagman_rescue_recov-node_conditional.pl
arguments    = $(nodename)
universe     = scheduler
output       = job_dagman_rescue_recov-$(nodename).out
error        = job_dagman_rescue_recov-$(nodename).err
# Note: this submit file uses the DAGMan default log file feature.
Notification = NEVER
queue
