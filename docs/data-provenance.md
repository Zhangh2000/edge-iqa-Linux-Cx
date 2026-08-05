# 数据来源与处理链

## 官方来源

项目原始图像来自国家卫星气象中心（NSMC）的风云卫星数据服务：

- [FY-4B Data Collection](https://satellite.nsmc.org.cn/DataPortal/en/data/dataset.html?satelliteCode=FY4B)
- [FENGYUN Satellite Data Service](https://satellite.nsmc.org.cn/DataPortal/en/home/index.html)

本仓库不提交原始图像、派生图像和演示视频。下载和使用数据时，应继续遵守数据服务页面当时有效的注册、引用和使用要求。

## 已确认元数据

当前批次包含 10 张图像。以下字段可从文件名和目录恢复：

| 字段 | 值 |
| --- | --- |
| 卫星 | FY-4B |
| 仪器 | AGRI |
| 覆盖代码 | `N_DISK_1050E` |
| 数据级别 | L2 |
| 产品代码 | `GCLR_MULT_NOM` |
| 标称分辨率 | 1000 m |
| 文件时间范围 | 2026-07-19 21:30:00 至 23:59:59 |
| 版本 | V0001 |
| 下载批次目录 | `C202607220749083663` |

逐文件清单见 [`data/manifests/source_metadata.csv`](../data/manifests/source_metadata.csv)。

## 无法补录的字段

下载时没有单独记录下列信息，因此文档不作推断：

- 实际下载日期；
- 门户检索条件和订单详情；
- 文件名时间字段采用的时区；
- 下载当时页面显示的具体许可条款。

清单将时间字段标记为 `unverified_filename_time`，将溯源状态标记为 `partially_reconstructed`。目录名看起来包含日期信息，但不能替代独立的下载记录。

## 数据处理链

```text
官方 FY-4B JPG（10992x11912）
  -> 固定 ROI (x=4472, y=4932, w=2048, h=2048)
  -> 无损参考 PNG
  -> 高斯噪声 / 高斯模糊 / JPEG 压缩（每张 9 个失真样本）
  -> pairs.csv（90 对）
  -> MSE / PSNR / SSIM 批处理结果
```

实时演示将 10 张参考 PNG 按文件名时间排序，缩放至 1024x1024 后编码为 2 FPS 视频。另生成一段“正常序列 + 暗化模糊序列”的退化视频，用于确定性地演示告警状态迁移。视频是测试输入，不是官方发布的视频产品。

## 后续规范

新下载数据时，应立即记录卫星、仪器、产品、空间分辨率、起止时间、时间基准、下载日期、来源 URL、订单号和许可说明，不能只依赖文件名事后恢复。
