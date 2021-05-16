#!/bin/bash
while true; do
    /coinflipper status coinflipper.coinflipper.svc.cluster.local > /pod-data/~status.txt
    mv /pod-data/~status.txt /pod-data/status.txt
done