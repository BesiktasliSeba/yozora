#!/usr/bin/env bash
# loads real sites in yozora-render on a device and fails on any crash signal, not part of meow test.
# variables: YOZORA_DEVICE (ssh target), YOZORA_DEVICE_DIR (render tool, fonts, ca-bundle.crt),
# YOZORA_DEVICE_RENDER (tool name), YOZORA_STRESS_SITES (urls), YOZORA_STRESS_ROUNDS, sshpass reads SSHPASS
set -euo pipefail

yozora_device="${YOZORA_DEVICE:?set YOZORA_DEVICE, for example root@DEVICE_ADDRESS}"
yozora_dir="${YOZORA_DEVICE_DIR:-/var/root/yozora-probe}"
yozora_render="${YOZORA_DEVICE_RENDER:-yozora-render.t}"
yozora_rounds="${YOZORA_STRESS_ROUNDS:-2}"
yozora_sites="${YOZORA_STRESS_SITES:-https://ru.wikipedia.org/wiki/Яблоко https://habr.com/ru/articles/ https://github.com/ https://vk.com/ https://www.reddit.com/ https://news.ycombinator.com/ https://pikabu.ru/ https://lenta.ru/ https://gemini.google.com/ https://developer.mozilla.org/ru/ https://www.youtube.com/}"
yozora_ssh=(sshpass -e ssh -o StrictHostKeyChecking=no -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedKeyTypes=+ssh-rsa -o LogLevel=ERROR -o ServerAliveInterval=15 -o ServerAliveCountMax=4)

failures=0
lost=0
runs=0
for ((round = 1; round <= yozora_rounds; round++)); do
    for site in ${yozora_sites}; do
        runs=$((runs + 1))
        # the page is rendered with the default timeouts, a hung host ends the run by itself
        log="$("${yozora_ssh[@]}" "${yozora_device}" "cd ${yozora_dir} && YOZORA_RENDER_SETTLE_MS=1500 ./${yozora_render} fonts ca-bundle.crt '${site}' stress.png 320 480 2>&1" || true)"
        if grep -q "crash signal" <<<"${log}"; then
            failures=$((failures + 1))
            echo "crash: round ${round} ${site}"
            grep -E "crash signal|pc=" <<<"${log}" | head -2
        elif ! grep -q "yozora-render: timing" <<<"${log}"; then
            # a dropped wifi link ends the run without a verdict, that is not a pass
            lost=$((lost + 1))
            echo "lost: round ${round} ${site}"
        else
            echo "ok: round ${round} ${site}"
        fi
    done
done
echo "${runs} runs, ${failures} crashes, ${lost} without a result"
[[ "${failures}" -eq 0 && "${lost}" -eq 0 ]]
