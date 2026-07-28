"""Run a precompiled RT-MonSter++ blob on an Intel NPU."""

import argparse
import glob
from pathlib import Path
from time import perf_counter

import cv2
import numpy as np
import openvino as ov


def load_image(path: str, height: int, width: int) -> tuple[np.ndarray, tuple[int, int]]:
    image = cv2.imread(path, cv2.IMREAD_COLOR)
    if image is None:
        raise FileNotFoundError(path)
    original_size = image.shape[:2]
    image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    if original_size != (height, width):
        image = cv2.resize(image, (width, height), interpolation=cv2.INTER_AREA)
    tensor = image.transpose(2, 0, 1)[None].astype(np.float32)
    return np.ascontiguousarray(tensor), original_size


def restore_disparity(disparity: np.ndarray, original_size: tuple[int, int], model_width: int) -> np.ndarray:
    disparity = np.asarray(disparity, dtype=np.float32).squeeze()
    original_height, original_width = original_size
    if disparity.shape != original_size:
        disparity = cv2.resize(
            disparity, (original_width, original_height), interpolation=cv2.INTER_LINEAR
        )
        # Disparity is a horizontal pixel distance, so resize its value as well.
        disparity *= original_width / model_width
    return disparity


def colorize(disparity: np.ndarray) -> np.ndarray:
    valid = np.isfinite(disparity)
    normalized = np.zeros(disparity.shape, dtype=np.uint8)
    if valid.any():
        low, high = np.percentile(disparity[valid], (2, 98))
        normalized[valid] = np.clip(
            (disparity[valid] - low) * 255.0 / max(high - low, 1e-6), 0, 255
        ).astype(np.uint8)
    return cv2.applyColorMap(normalized, cv2.COLORMAP_INFERNO)


def main() -> None:
    parser = argparse.ArgumentParser(description="RT-MonSter++ Intel NPU inference")
    parser.add_argument("--blob", required=True)
    parser.add_argument("--left", required=True, help="left image or glob")
    parser.add_argument("--right", required=True, help="right image or glob")
    parser.add_argument("--output", default="npu_output")
    args = parser.parse_args()

    left_images = sorted(glob.glob(args.left))
    right_images = sorted(glob.glob(args.right))
    if not left_images or len(left_images) != len(right_images):
        raise SystemExit(
            f"左右图片数量错误：left={len(left_images)}, right={len(right_images)}"
        )

    core = ov.Core()
    if not any(device.startswith("NPU") for device in core.available_devices):
        raise SystemExit(f"OpenVINO 没有发现 NPU：{core.available_devices}")
    # OpenVINO 2026.1's Python binding expects bytes (or io.BytesIO) here;
    # passing a BufferedReader raises TypeError on Python 3.14.
    compiled = core.import_model(Path(args.blob).read_bytes(), "NPU")

    left_port, right_port = compiled.inputs
    output_port = compiled.output(0)
    _, _, model_height, model_width = left_port.shape
    model_height, model_width = int(model_height), int(model_width)
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    request = compiled.create_infer_request()
    elapsed = []
    for left_path, right_path in zip(left_images, right_images):
        left, original_size = load_image(left_path, model_height, model_width)
        right, right_size = load_image(right_path, model_height, model_width)
        if original_size != right_size:
            raise SystemExit(f"左右原图尺寸不一致：{left_path} / {right_path}")

        started = perf_counter()
        result = request.infer({left_port: left, right_port: right})
        elapsed.append((perf_counter() - started) * 1000.0)
        disparity = restore_disparity(result[output_port], original_size, model_width)
        if not np.isfinite(disparity).all():
            raise RuntimeError(f"输出包含 NaN/Inf：{left_path}")

        stem = Path(left_path).stem
        np.save(output_dir / f"{stem}.npy", disparity)
        disparity16 = np.clip(np.rint(disparity * 256.0), 0, 65535).astype(np.uint16)
        cv2.imwrite(str(output_dir / f"{stem}_disp16.png"), disparity16)
        cv2.imwrite(str(output_dir / f"{stem}_color.png"), colorize(disparity))

    timings = np.asarray(elapsed)
    print(f"Device: NPU | model input: {model_width}x{model_height}")
    print(f"Images: {len(elapsed)} | output: {output_dir.resolve()}")
    print(f"Average: {timings.mean():.2f} ms | P95: {np.percentile(timings, 95):.2f} ms")
    print(f"FPS: {1000.0 / timings.mean():.2f}")


if __name__ == "__main__":
    main()
