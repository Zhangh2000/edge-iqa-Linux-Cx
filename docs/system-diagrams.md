# 系统功能与架构图

## 1. 系统功能层级流转图

这张图从“系统能做什么”出发，展示输入、检测、调度、告警和结果管理之间的功能层级及三条运行路径。

```mermaid
flowchart TB
    SYS["Linux 边缘端气象图像质量检测与异常告警系统"]

    SYS --> IN["1. 图像输入"]
    SYS --> QA["2. 质量检测"]
    SYS --> RT["3. 实时调度"]
    SYS --> NET["4. 网络传输"]
    SYS --> AL["5. 异常告警"]
    SYS --> OUT["6. 结果管理"]

    IN --> IN1["参考图 + 待测图"]
    IN --> IN2["pairs.csv 批处理清单"]
    IN --> IN3["本地视频 / 摄像头"]
    IN --> IN4["TCP JPEG 图像"]

    QA --> QA1["输入解码与尺寸/通道校验"]
    QA --> QA2["离线全参考指标\nMSE / PSNR / SSIM"]
    QA --> QA3["实时无参考指标\n亮度 / 拉普拉斯清晰度"]
    QA --> QA4["运行指标\nFPS / 单帧处理延迟"]

    RT --> RT1["采集线程"]
    RT --> RT2["线程安全有界队列"]
    RT --> RT3["处理线程"]
    RT1 --> RT2 --> RT3
    RT2 --> RT4["文件：队满阻塞"]
    RT2 --> RT5["摄像头：丢弃最旧帧"]

    NET --> NET1["EIQ1 16 字节包头"]
    NET --> NET2["Payload 长度 + CRC32"]
    NET --> NET3["循环 send/recv 处理短读短写"]

    AL --> AL1["阈值判断"]
    AL --> AL2["连续违规计数"]
    AL --> AL3["UNASSESSED / NORMAL\nWARNING / ALARM"]
    AL --> AL4["连续正常恢复滞回"]
    AL1 --> AL2 --> AL3 --> AL4

    OUT --> OUT1["单图终端结果"]
    OUT --> OUT2["批处理 CSV"]
    OUT --> OUT3["实时画面叠加"]
    OUT --> OUT4["逐帧 CSV"]
    OUT --> OUT5["TCP 文本结果响应"]

    IN1 --> QA1 --> QA2 --> OUT1
    IN2 --> QA1 --> QA2 --> OUT2
    IN3 --> RT1
    RT3 --> QA3 --> QA4 --> AL1
    AL3 --> OUT3
    AL3 --> OUT4
    IN4 --> NET1 --> NET2 --> NET3 --> QA1
    AL3 --> OUT5
```

### 三条主流程

1. **离线检测：** 图像对或 `pairs.csv` -> 解码校验 -> MSE/PSNR/SSIM -> 终端或 CSV。
2. **实时检测：** 视频/摄像头 -> 采集线程 -> 有界队列 -> 处理线程 -> 亮度/清晰度/FPS/延迟 -> 告警 -> Overlay/CSV。
3. **网络检测：** 客户端 JPEG -> TCP 自定义协议 -> 服务端解码 -> 实时指标与告警 -> TCP 结果响应。

## 2. 系统模块框图

这张图从“代码如何执行”出发，按输入适配层、核心业务层、输出层和基础技术层划分模块。

```mermaid
flowchart LR
    subgraph INPUT["输入与适配层"]
        FILE["图像文件 / pairs.csv"]
        VIDEO["视频文件 / 摄像头"]
        CLIENT["TCP 客户端"]
        IMGIO["image_io\ncv::imread + 合法性校验"]
        VSRC["video_source\ncv::VideoCapture"]
        TCPIN["tcp_transport\nSocket + EIQ1 + CRC32"]

        FILE --> IMGIO
        VIDEO --> VSRC
        CLIENT --> TCPIN
    end

    subgraph CORE["核心业务层"]
        GEN["distortions\n噪声 / 模糊 / JPEG"]
        CAP["① 采集线程"]
        QUEUE["② BoundedQueue\nmutex + condition_variable"]
        PROC["③ 处理线程"]
        FR["metrics\nMSE / PSNR / SSIM"]
        NR["frame_metrics\n亮度 / 拉普拉斯方差"]
        PERF["FPS / latency 统计"]
        ALERT["alert\n连续帧告警状态机"]

        CAP --> QUEUE --> PROC
        PROC --> NR --> PERF --> ALERT
    end

    subgraph OUTPUT["输出与管理层"]
        TERM["终端指标"]
        BCSV["批处理 CSV"]
        VIEW["实时 Overlay"]
        SCSV["逐帧 CSV"]
        RESP["TCP 结果响应"]
    end

    IMGIO --> GEN --> FR
    IMGIO --> FR
    IMGIO --> NR
    VSRC --> CAP
    TCPIN --> IMGIO

    FR --> TERM
    FR --> BCSV
    ALERT --> VIEW
    ALERT --> SCSV
    ALERT --> RESP

    subgraph STACK["基础技术栈与运行环境"]
        CPP["C++17 / STL / RAII"]
        OCV["OpenCV\ncore · imgproc · imgcodecs · videoio · highgui"]
        OS["Linux API\nPOSIX Socket · Thread Synchronization"]
        BUILD["CMake · GCC/AppleClang · Git"]
        DEPLOY["Ubuntu 24.04 ARM64 Docker\n目标平台：Linux 边缘设备"]
        CPP --- OCV --- OS --- BUILD --- DEPLOY
    end

    STACK -. "提供编译与运行基础" .-> INPUT
    STACK -. "提供编译与运行基础" .-> CORE
    STACK -. "提供编译与运行基础" .-> OUTPUT
```

## 3. 模块与代码对应关系

| 系统模块 | 主要代码 | 关键技术 |
| --- | --- | --- |
| 离线评价 | `metrics.*`、`iqa_eval` | MSE、PSNR、SSIM、OpenCV Mat |
| 数据集生成 | `distortions.*`、`iqa_generate` | GaussianBlur、RNG、JPEG 编码 |
| 图像输入 | `image_io.*`、`video_source.*` | imread、VideoCapture、输入校验 |
| 实时检测 | `frame_metrics.*`、`iqa_stream` | 灰度均值、Laplacian、FPS、latency |
| 多线程调度 | `bounded_queue.hpp`、`iqa_stream` | std::thread、mutex、condition_variable |
| 告警判断 | `alert.*` | 阈值、连续帧计数、恢复滞回 |
| 网络传输 | `tcp_transport.*`、TCP client/server | POSIX Socket、TCP、CRC32、自定义协议 |
| 结果输出 | `csv_writer.*`、`stream_csv_writer.*` | CSV 转义、批处理与逐帧日志 |
| 工程构建 | `CMakeLists.txt`、`Dockerfile` | CMake、GCC、Ubuntu ARM64 Docker |

## 4. 一帧图像的执行过程

1. `VideoCapture` 从视频文件或摄像头读取一帧 `cv::Mat`。
2. 采集线程把帧及序号、采集时间写入有界队列。
3. 处理线程取出帧，转换为灰度图并计算亮度和拉普拉斯方差。
4. 程序统计单帧处理延迟和滑动 FPS。
5. 告警状态机根据阈值、连续违规帧数和恢复帧数更新状态。
6. 程序将指标和状态叠加到画面，并写入逐帧 CSV。
7. 如果图像来自 TCP，服务端把指标与告警状态封装为结果包返回客户端。

## 5. 图中必须说明的边界

- MSE、PSNR、SSIM 需要参考图，只用于离线全参考检测。
- 实时链路没有严格配准的参考帧，当前使用亮度和清晰度指标。
- CRC32 用于发现数据损坏，不提供加密和身份认证。
- ARM64 Docker 验证 Linux 用户态兼容性，不等于树莓派实机性能测试。
