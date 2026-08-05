# 快速演示指南

所有命令都从项目根目录执行。主线演示依次为：离线 90 对、真实气象视频告警、TCP 回环。

## 1. macOS 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix opencv)"
cmake --build build --parallel 2
```

## 2. 离线批处理

```bash
./build/iqa_eval --batch data/manifests/pairs.csv --out results/phase1_metrics_final.csv
```

预期：`batch total: 90`、`batch succeeded: 90`、`batch failed: 0`。

## 3. 生成真实气象演示视频

需要项目私有的 `data/reference/*.png` 和本机 FFmpeg。正常序列：

```bash
ffmpeg -hide_banner -loglevel error -y -framerate 2 -pattern_type glob -i 'data/reference/*.png' -vf 'scale=1024:1024:flags=lanczos,format=yuv420p' -c:v libx264 -preset fast -crf 20 -movflags +faststart data/videos/fy4b-sequence.mp4
```

在正常序列后拼接暗化、模糊序列：

```bash
ffmpeg -hide_banner -loglevel error -y -i data/videos/fy4b-sequence.mp4 -filter_complex '[0:v]split=2[normal][bad];[normal]setpts=PTS-STARTPTS[n];[bad]gblur=sigma=6,eq=brightness=-0.25,setpts=PTS-STARTPTS[b];[n][b]concat=n=2:v=1:a=0,format=yuv420p[out]' -map '[out]' -r 2 -c:v libx264 -preset fast -crf 20 -movflags +faststart data/videos/fy4b-sequence-degraded.mp4
```

## 4. 气象告警演示

先跑正常序列：

```bash
./build/iqa_stream --video data/videos/fy4b-sequence.mp4 --brightness-min 50 --sharpness-min 1000 --alert-frames 3 --recovery-frames 3 --out results/stream/fy4b-normal.csv
```

再跑退化序列：

```bash
./build/iqa_stream --video data/videos/fy4b-sequence-degraded.mp4 --brightness-min 50 --sharpness-min 1000 --alert-frames 3 --recovery-frames 3 --out results/stream/fy4b-degraded.csv
```

预期：正常序列无告警；退化序列从 `NORMAL` 经过两帧 `WARNING` 进入 `ALARM`。按 `q` 或 Escape 退出窗口。

无图形界面时增加 `--headless`，也可用 `--max-frames 20` 限制处理帧数。

## 5. TCP 回环演示

终端 A：

```bash
./build/iqa_tcp_server --bind 127.0.0.1 --port 19090 --brightness-min 50 --sharpness-min 1000
```

终端 B：

```bash
./build/iqa_tcp_client --host 127.0.0.1 --port 19090 --image data/reference/FY4B-_AGRI--_N_DISK_1050E_L2-_GCLR_MULT_NOM_20260719213000_20260719214459_1000M_V0001.png
```

演示重点不是端口号，而是说明 TCP 字节流需要自定义包头、负载长度、循环收发和 CRC32 完整性检查。

## 6. Linux ARM64 复验

```bash
docker run --rm -it --platform linux/arm64 --name edge-iqa-final --mount type=bind,source="$(pwd)",target=/workspace edge-iqa-dev:ubuntu24.04 bash
```

看到 `root@容器ID:/workspace#` 后，在容器内逐条执行：

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --parallel 2
./build-linux/iqa_eval --batch data/manifests/pairs.csv --out /tmp/phase1-final.csv
./build-linux/iqa_stream --video data/videos/fy4b-sequence-degraded.mp4 --headless --brightness-min 50 --sharpness-min 1000 --max-frames 20 --out /tmp/stream-final.csv
```

批处理计算 SSIM 时可能数分钟不输出中间进度。等待 `90/90` 汇总，不要把下一条命令粘贴到正在运行的前台程序中。

## 演示陈述顺序

1. 先用架构图说明离线、流式、网络三条数据链；
2. 展示正常与退化气象视频的状态迁移；
3. 打开逐帧 CSV，说明指标、延迟和告警原因可追溯；
4. 说明有界队列的文件阻塞和摄像头丢旧帧策略；
5. 说明 Docker ARM64 验证边界，明确尚未做树莓派实机测试。
