#!/bin/bash
set -e

while true; do
    /coinflipper status coinflipper.coinflipper.svc.cluster.local > /pod-data/~status.txt
    total_flips=$(grep "Total coins flipped" /pod-data/~status.txt | cut -d ":" -f 2 | sed -e 's/[ ,]//g')

    echo '# HELP coinflipper_total_flips Total number of coins flipped' > /pod-data/~metrics.txt
    echo '# TYPE coinflipper_total_flips counter' >> /pod-data/~metrics.txt
    echo "coinflipper_total_flips $total_flips" >> /pod-data/~metrics.txt

    mv /pod-data/~status.txt /pod-data/status.txt
    mv /pod-data/~metrics.txt /pod-data/metrics
done
