#include <sys/time.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sched.h>

#include "spsc_queue.h"
#include "Vtestbench__Dpi.h"

static struct timeval stop_time, start_time;

spsc_queue rxq;
spsc_queue txq;

svLogic pi_umi_init(int rx_port, int tx_port) {
    // determine RX URI
    char rx_uri[128];
    sprintf(rx_uri, "/tmp/feeds-%d", rx_port);
    spsc_open(&rxq, rx_uri);

    // determine TX URI
    char tx_uri[128];
    sprintf(tx_uri, "/tmp/feeds-%d", tx_port);
    spsc_open(&txq, tx_uri);

    // unused return value
    return 0;
}

svLogic pi_umi_recv(int* success, svBitVecVal* rbuf){
    *success = spsc_recv(&rxq, (int*)rbuf);
    return 0;
}

svLogic pi_umi_send(int* success, const svBitVecVal* sbuf) {
    *success = spsc_send(&txq, (int*)sbuf);
    return 0;
}

svLogic pi_time_taken(double* t) {
    // compute time taken in seconds
	gettimeofday(&stop_time, NULL);
    unsigned long t_us = 0;
    t_us += ((stop_time.tv_sec - start_time.tv_sec) * 1000000);
    t_us += (stop_time.tv_usec - start_time.tv_usec);
    *t = 1.0e-6*t_us;
    gettimeofday(&start_time, NULL);

    // unused return value
    return 0;
}