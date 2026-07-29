# edge-iqa

`edge-iqa` is a small C++17/OpenCV command-line project for full-reference
image quality assessment.

## Layout

- `CMakeLists.txt`: CMake build entry point.
- `Dockerfile`: Ubuntu 24.04 C++/OpenCV development image definition.
- `.dockerignore`: excludes datasets, results, and host build outputs from the Docker build context.
- `apps/iqa_eval/main.cpp`: image quality evaluation command-line entry point.
- `apps/iqa_generate/main.cpp`: deterministic distortion dataset generator.
- `include/iqa/metrics.hpp`: metric API declarations.
- `include/iqa/image_io.hpp`: image loading API declarations.
- `include/iqa/batch_runner.hpp`: batch evaluation API declarations.
- `include/iqa/csv_writer.hpp`: CSV output API declarations.
- `include/iqa/distortions.hpp`: distortion algorithm API declarations.
- `src/metrics.cpp`: MSE, PSNR, and SSIM implementation.
- `src/image_io.cpp`: OpenCV image loading and validation.
- `src/batch_runner.cpp`: manifest-driven batch evaluation.
- `src/csv_writer.cpp`: CSV file creation and escaping.
- `src/distortions.cpp`: Gaussian noise, Gaussian blur, and JPEG encoding.
- `data/manifests/pairs.csv`: batch input manifest template.
- `data/raw/`: suggested location for reference images.
- `data/reference/`: lossless prepared references created by `iqa_generate`.
- `data/distorted/`: suggested location for distorted images.
- `results/`: suggested location for output CSV files.
- `tests/`: reserved for later unit tests.

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

## Metrics

- `mse_standard`: `sum(error^2) / (width * height * channels)`.
- `psnr_db`: `10 * log10(255^2 / mse_standard)`.
- `ssim`: grayscale SSIM using an 11x11 Gaussian window.
