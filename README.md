# edge-iqa

`edge-iqa` is a C++17/OpenCV image quality detection and anomaly alert system
for Linux edge environments. It supports offline full-reference assessment,
camera or video stream inspection, a bounded producer-consumer pipeline, and
framed TCP image transport.

## Status

- Offline MSE/PSNR/SSIM evaluation and deterministic distortion generation.
- Manifest-driven batch evaluation: 90/90 FY-4B image pairs verified.
- Stream brightness, Laplacian sharpness, FPS, latency, overlay, CSV, and alert state machine.
- Thread-safe bounded queue with separate file and camera backpressure policies.
- TCP image/result packets with a 16-byte header, complete I/O loops, and CRC32.
- macOS ARM64 full regression and Ubuntu 24.04 ARM64 Docker build verification.

The project has not yet been validated on a physical Raspberry Pi or a real
V4L2 camera. Docker verifies the ARM64 Linux user-space build, not device
drivers, hardware performance, temperature, power, or long-running stability.

## Documentation

- [Architecture](docs/architecture.md)
- [System function and module diagrams](docs/system-diagrams.md)
- [Data provenance](docs/data-provenance.md)
- [Test report](docs/test-report.md)
- [Demo guide](docs/demo-guide.md)
- [Resume and interview notes](docs/resume.md)

## Layout

- `CMakeLists.txt`: CMake build entry point.
- `Dockerfile`: Ubuntu 24.04 C++/OpenCV development image definition.
- `.dockerignore`: excludes datasets, results, and host build outputs from the Docker build context.
- `apps/iqa_eval/main.cpp`: image quality evaluation command-line entry point.
- `apps/iqa_generate/main.cpp`: deterministic distortion dataset generator.
- `apps/iqa_stream/main.cpp`: camera/video stream command-line entry point.
- `apps/iqa_tcp_server/main.cpp`: TCP image receiver, evaluator, and result server.
- `apps/iqa_tcp_client/main.cpp`: JPEG image sender and result client.
- `include/iqa/metrics.hpp`: metric API declarations.
- `include/iqa/image_io.hpp`: image loading API declarations.
- `include/iqa/batch_runner.hpp`: batch evaluation API declarations.
- `include/iqa/csv_writer.hpp`: CSV output API declarations.
- `include/iqa/distortions.hpp`: distortion algorithm API declarations.
- `include/iqa/alert.hpp`: configurable consecutive-frame alert state machine.
- `include/iqa/bounded_queue.hpp`: thread-safe bounded producer-consumer queue.
- `include/iqa/tcp_transport.hpp`: framed TCP packet and RAII socket API.
- `include/iqa/frame_metrics.hpp`: per-frame brightness and sharpness metric API.
- `include/iqa/stream_csv_writer.hpp`: per-frame stream CSV output API.
- `include/iqa/video_source.hpp`: OpenCV video source interface and metadata.
- `src/metrics.cpp`: MSE, PSNR, and SSIM implementation.
- `src/image_io.cpp`: OpenCV image loading and validation.
- `src/batch_runner.cpp`: manifest-driven batch evaluation.
- `src/csv_writer.cpp`: CSV file creation and escaping.
- `src/distortions.cpp`: Gaussian noise, Gaussian blur, and JPEG encoding.
- `src/alert.cpp`: normal, warning, alarm, and recovery state transitions.
- `src/tcp_transport.cpp`: complete send/receive loops, CRC32, and socket lifecycle.
- `src/frame_metrics.cpp`: grayscale mean and Laplacian-variance implementation.
- `src/stream_csv_writer.cpp`: per-frame stream CSV creation and escaping.
- `src/video_source.cpp`: `cv::VideoCapture` camera/video input implementation.
- `data/manifests/pairs.csv`: batch input manifest template.
- `data/manifests/source_metadata.csv`: reconstructed FY-4B source metadata.
- `data/raw/`: suggested location for reference images.
- `data/reference/`: lossless prepared references created by `iqa_generate`.
- `data/distorted/`: suggested location for distorted images.
- `results/`: suggested location for output CSV files.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Linux ARM64 development container

The development image uses Ubuntu 24.04 and installs GCC/G++, CMake,
pkg-config, and the OpenCV development package. The source tree and datasets
are not copied into the image; mount the project at `/workspace` when starting
the container.

Build the ARM64 development image from the project root:

```bash
docker build --platform linux/arm64 -t edge-iqa-dev:ubuntu24.04 .
```

Start a disposable container with a writable bind mount:

```bash
docker run --rm -it --platform linux/arm64 \
  --name edge-iqa-linux \
  --mount type=bind,source="$(pwd)",target=/workspace \
  edge-iqa-dev:ubuntu24.04
```

Use a separate Linux build directory inside the container:

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --parallel 2
./build-linux/iqa_eval \
  --batch data/manifests/pairs.csv \
  --out results/phase1_metrics_linux.csv
```

`build/` remains the macOS build tree. Do not reuse it in Linux because CMake
caches absolute paths, compiler information, platform checks, and dependency
locations for the environment that configured the directory.

## Generate a distortion dataset

For the current `10992x11912` FY-4B images, use the fixed centered
`2048x2048` ROI to keep storage and evaluation memory bounded:

```bash
./build/iqa_generate \
  --input data/raw/C202607220749083663 \
  --reference-dir data/reference \
  --output-dir data/distorted \
  --manifest data/manifests/pairs.csv \
  --roi 4472 4932 2048 2048 \
  --seed 20260723
```

The generator writes one lossless reference PNG and nine distorted images per
source. Noise and blur outputs are PNG; JPEG outputs contain a single JPEG
encoding at qualities 80, 50, and 20. The generated manifest includes paths,
distortion levels, parameters, and the deterministic seed used for noise.

Images above 25 megapixels require either `--roi` or the explicit
`--full-resolution` option. Full-resolution generation is supported but is not
recommended for the current FY-4B dataset because of memory and disk usage.

## Single image mode

```bash
./build/iqa_eval --ref data/raw/reference.jpg --dist data/distorted/reference_noise.jpg
```

## Batch mode

```bash
./build/iqa_eval --batch data/manifests/pairs.csv --out results/results.csv
```

## Video stream mode

Open a local video and display frames with a measured FPS overlay:

```bash
./build/iqa_stream --video data/videos/sample.mp4
```

The overlay reports per-frame brightness, Laplacian-variance sharpness,
processing latency, smoothed FPS, and alert status. Status remains
`UNASSESSED` until at least one explicit threshold is provided.

Frame capture and metric evaluation form a producer-consumer pipeline backed
by a bounded queue. File input blocks when the queue is full so every frame is
preserved. Camera input drops the oldest queued frame to bound live latency.

Write one CSV row per successfully processed frame:

```bash
./build/iqa_stream \
  --video data/videos/sample.mp4 \
  --queue-capacity 4 \
  --out results/stream/sample.csv \
  --brightness-min 80 \
  --sharpness-min 500 \
  --alert-frames 3 \
  --recovery-frames 3
```

Thresholds are dataset- and resolution-dependent. The values above only show
the interface and must be calibrated on representative data before use.

Press `q` or Escape to stop. A camera is optional and can be opened by index:

```bash
./build/iqa_stream --camera 0
```

For Docker, SSH, or automated checks without a graphical display, use headless
mode and set a frame limit:

```bash
./build-linux/iqa_stream \
  --video data/videos/sample.mp4 \
  --headless \
  --max-frames 100
```

### FY-4B anomaly demo

The private dataset can be converted into a normal sequence and a deterministic
normal-plus-degraded sequence by following [the demo guide](docs/demo-guide.md).
The calibrated thresholds below are valid only for the current 1024x1024 demo:

```bash
./build/iqa_stream \
  --video data/videos/fy4b-sequence-degraded.mp4 \
  --brightness-min 50 \
  --sharpness-min 1000 \
  --alert-frames 3 \
  --recovery-frames 3 \
  --out results/stream/fy4b-degraded.csv
```

The verified sequence remains `NORMAL` for the first 10 frames, transitions
through two `WARNING` frames, and enters `ALARM` for the last eight dark and
blurred frames.

## TCP image mode

The TCP protocol uses a fixed 16-byte header followed by a variable-length
payload. The header contains the `EIQ1` magic, packet type, payload length, and
CRC32. Complete send/receive loops handle TCP partial I/O, and the server
rejects invalid magic, oversized payloads, CRC failures, and undecodable data.

Start a server bound to the local machine:

```bash
./build/iqa_tcp_server \
  --bind 127.0.0.1 \
  --port 9000 \
  --brightness-min 80 \
  --sharpness-min 500
```

Send one or more images over a single TCP connection:

```bash
./build/iqa_tcp_client \
  --host 127.0.0.1 \
  --port 9000 \
  --image data/reference/example-1.png \
  --image data/reference/example-2.png
```

Use `--bind 0.0.0.0` only when the server must accept LAN connections, and
apply host firewall rules before exposing the port. The current protocol is a
project demonstration protocol; it does not provide encryption or client
authentication.

## Metrics

- `mse_standard`: `sum(error^2) / (width * height * channels)`.
- `psnr_db`: `10 * log10(255^2 / mse_standard)`.
- `ssim`: grayscale SSIM using an 11x11 Gaussian window.
