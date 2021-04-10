#!/bin/bash
set -e
statusfile="/pod-data/status.txt"
fileage=`date -d "now - $(stat -c "%Y" $statusfile) seconds" +%s`
if [[ $fileage -gt 600 ]]; then
    >&2 echo "File too old ($fileage seconds)"
    exit 1
fi