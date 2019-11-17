#!/bin/bash
while true; do
    /coinflipper status coinflipper.default > /pod-data/~status.txt
    mv /pod-data/~status.txt /pod-data/status.txt
done