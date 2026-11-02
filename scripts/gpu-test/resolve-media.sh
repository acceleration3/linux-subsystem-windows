#!/usr/bin/env bash
set -euo pipefail
out="${1:?usage: resolve-media.sh OUT_DIR}"
mkdir -p "${out}"
ff=(ffmpeg -hide_banner -loglevel error -y)
"${ff[@]}" -f lavfi -i "smptehdbars=size=1280x720:rate=25:duration=4" \
    -c:v prores_ks -profile:v 0 -pix_fmt yuv422p10le "${out}/bars.mov"
"${ff[@]}" -f lavfi -i "testsrc2=size=1280x720:rate=25:duration=4" \
    -c:v libx264 -profile:v high -pix_fmt yuv420p -preset medium -crf 18 -g 25 -bf 0 "${out}/pattern.mp4"
"${ff[@]}" -f lavfi -i "mandelbrot=size=1280x720:rate=25:end_pts=100:start_scale=3:end_scale=0.3" -t 4 \
    -c:v dnxhd -profile:v dnxhr_lb -pix_fmt yuv422p "${out}/fractal.mov"
"${ff[@]}" -f lavfi -i "testsrc2=size=1920x1080:rate=1" -frames:v 1 "${out}/still.png"
"${ff[@]}" -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=4" \
    -f lavfi -i "sine=frequency=660:sample_rate=48000:duration=4" \
    -filter_complex "[0:a][1:a]join=inputs=2:channel_layout=stereo[a]" -map "[a]" -c:a pcm_s24le "${out}/tone.wav"
ls -l "${out}"
