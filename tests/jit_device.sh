#!/usr/bin/env bash
# runs the jit script tests on a device and compares them with the c-loop shell, not part of meow test.
# YOZORA_DEVICE (ssh target), YOZORA_DEVICE_DIR (holds yozora-js-jit and the c-loop yozora-js), sshpass reads
# SSHPASS
set -euo pipefail

yozora_root="$(cd "$(dirname "$0")/.." && pwd)"
yozora_device="${YOZORA_DEVICE:?set YOZORA_DEVICE, for example root@DEVICE_ADDRESS}"
yozora_dir="${YOZORA_DEVICE_DIR:-/var/root/yozora-probe}"
yozora_cloop="${YOZORA_DEVICE_CLOOP:-yozora-js-cloop}"
yozora_ssh=(sshpass -e ssh -o StrictHostKeyChecking=yes -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedKeyTypes=+ssh-rsa)
yozora_scp=(sshpass -e scp -O -q -o StrictHostKeyChecking=yes -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedKeyTypes=+ssh-rsa)

for yozora_script in features jit-stress jit-regexp ic-stress dfg-properties dfg-soft-float dfg-collections; do
    "${yozora_scp[@]}" "${yozora_root}/tests/js/${yozora_script}.js" "${yozora_device}:${yozora_dir}/"
    # low thresholds compile almost every function, the default run keeps the concurrent compiler on
    "${yozora_ssh[@]}" "${yozora_device}" "set -e; cd ${yozora_dir} && ./${yozora_cloop} ${yozora_script}.js > ${yozora_script}.cloop 2>&1
        JSC_useJIT=1 JSC_useRegExpJIT=1 JSC_useDFGJIT=1 ./yozora-js-jit ${yozora_script}.js > ${yozora_script}.jit 2>&1
        JSC_useJIT=1 JSC_useRegExpJIT=1 JSC_useDFGJIT=0 JSC_useConcurrentJIT=0 JSC_thresholdForJITAfterWarmUp=5 JSC_thresholdForJITSoon=1 ./yozora-js-jit ${yozora_script}.js > ${yozora_script}.low 2>&1
        JSC_useJIT=1 JSC_useDFGJIT=1 JSC_useConcurrentJIT=0 JSC_thresholdForJITAfterWarmUp=5 JSC_thresholdForJITSoon=1 JSC_thresholdForOptimizeAfterWarmUp=10 JSC_thresholdForOptimizeSoon=5 JSC_reportDFGCompileTimes=1 ./yozora-js-jit ${yozora_script}.js > ${yozora_script}.dfg 2> ${yozora_script}.dfg.log
        JSC_useJIT=1 JSC_useDFGJIT=1 JSC_useConcurrentJIT=1 JSC_thresholdForJITAfterWarmUp=5 JSC_thresholdForJITSoon=1 JSC_thresholdForOptimizeAfterWarmUp=10 JSC_thresholdForOptimizeSoon=5 ./yozora-js-jit ${yozora_script}.js > ${yozora_script}.concurrent 2> ${yozora_script}.concurrent.log
        cmp ${yozora_script}.cloop ${yozora_script}.jit
        cmp ${yozora_script}.cloop ${yozora_script}.low
        cmp ${yozora_script}.cloop ${yozora_script}.dfg
        cmp ${yozora_script}.cloop ${yozora_script}.concurrent
        case ${yozora_script} in dfg-*) grep -q 'using DFG' ${yozora_script}.dfg.log;; esac"
    echo "jit: ${yozora_script} matches the c-loop output"
done
