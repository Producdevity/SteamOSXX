#!/bin/bash
# Queues out/steamos-rg40xx.img for the elevated SD writer (C:\Users\kurt\sdflash\sdflash-helper.ps1)
# and waits for it to finish. The helper only ever writes to the USB card reader.
set -euo pipefail
cd "$(dirname "$0")/.."
q=/c/Users/kurt/sdflash/queue
job=steamos-$(date +%Y%m%d-%H%M%S)
cp out/steamos-rg40xx.img "$q/$job.img"
: > "$q/$job.go"
echo "flash: queued $job"
until [ -f "$q/$job.done" ] || [ -f "$q/$job.failed" ]; do sleep 5; done
if [ -f "$q/$job.failed" ]; then echo "flash: FAILED: $(cat "$q/$job.failed")"; exit 1; fi
rm -f "$q/$job.img"
echo "flash: done"
