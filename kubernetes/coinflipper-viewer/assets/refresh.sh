#!/bin/bash
set -e

while true; do
    /coinflipper status coinflipper.coinflipper.svc.cluster.local > /pod-data/~status.txt

    total_flips=$(grep "Total coins flipped" /pod-data/~status.txt | cut -d ":" -f 2 | sed -e 's/[ ,]//g')
    connected_clients=$(grep -E " cps$" /pod-data/~status.txt | wc -l)

    if [[ -z "$total_flips" ]]; then
       rm /pod-data/metrics
    else
        echo '# HELP coinflipper_total_flips Total number of coins flipped' > /pod-data/~metrics.txt
        echo '# TYPE coinflipper_total_flips counter' >> /pod-data/~metrics.txt
        echo "coinflipper_total_flips $total_flips" >> /pod-data/~metrics.txt
        echo '' >> /pod-data/~metrics.txt
        echo '# HELP coinflipper_connected_clients Number of connected clients' >> /pod-data/~metrics.txt
        echo '# TYPE coinflipper_connected_clients gauge' >> /pod-data/~metrics.txt
        echo "coinflipper_connected_clients $connected_clients" >> /pod-data/~metrics.txt

        mv /pod-data/~metrics.txt /pod-data/metrics
    fi

    mv /pod-data/~status.txt /pod-data/status.txt
done
