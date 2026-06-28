#!/usr/bin/env python3
"""Rewrite a rosbag2 directory and convert yuyv Image messages to bgr8."""

import argparse
import shutil
import sys
from pathlib import Path

import cv2
import numpy as np
import rosbag2_py
from rclpy.serialization import deserialize_message, serialize_message
from rosidl_runtime_py.utilities import get_message
from sensor_msgs.msg import Image


YUYV_ENCODINGS = {"yuyv", "yuv422", "yuv422_yuy2"}


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Rewrite a rosbag2 bag with message indexes and convert yuyv "
            "sensor_msgs/Image topics to bgr8."
        )
    )
    parser.add_argument("input_bag", help="Input rosbag2 directory")
    parser.add_argument(
        "output_bag",
        nargs="?",
        help="Output rosbag2 directory. Default: <input_bag>_bgr8",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Remove output directory first if it already exists.",
    )
    parser.add_argument(
        "--fastwrite",
        action="store_true",
        help="Write MCAP with fastwrite. This is faster but omits message indexes.",
    )
    return parser.parse_args()


def storage_id_for_bag(path):
    metadata = path / "metadata.yaml"
    if not metadata.exists():
        raise RuntimeError(f"{path} does not look like a rosbag2 directory")
    for line in metadata.read_text(encoding="utf-8").splitlines():
        if line.strip().startswith("storage_identifier:"):
            return line.split(":", 1)[1].strip() or "mcap"
    return "mcap"


def convert_image(msg):
    encoding = msg.encoding.lower()
    if encoding == "bgr8":
        return msg, False
    if encoding == "rgb8":
        rgb = np.frombuffer(msg.data, dtype=np.uint8).reshape((msg.height, msg.width, 3))
        bgr = cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR)
    elif encoding in YUYV_ENCODINGS:
        yuyv = np.frombuffer(msg.data, dtype=np.uint8).reshape((msg.height, msg.width, 2))
        bgr = cv2.cvtColor(yuyv, cv2.COLOR_YUV2BGR_YUY2)
    else:
        return msg, False

    out = Image()
    out.header = msg.header
    out.height = msg.height
    out.width = msg.width
    out.encoding = "bgr8"
    out.is_bigendian = 0
    out.step = msg.width * 3
    out.data = bgr.tobytes()
    return out, True


def main():
    args = parse_args()
    input_bag = Path(args.input_bag).expanduser().resolve()
    output_bag = Path(args.output_bag).expanduser().resolve() if args.output_bag else Path(
        f"{input_bag}_bgr8"
    )

    if not input_bag.exists():
        raise RuntimeError(f"Input bag does not exist: {input_bag}")
    if output_bag.exists():
        if not args.force:
            raise RuntimeError(f"Output already exists, use --force to replace: {output_bag}")
        shutil.rmtree(output_bag)

    storage_id = storage_id_for_bag(input_bag)
    converter_options = rosbag2_py.ConverterOptions("", "")

    reader = rosbag2_py.SequentialReader()
    reader.open(
        rosbag2_py.StorageOptions(uri=str(input_bag), storage_id=storage_id),
        converter_options,
    )
    if hasattr(reader, "set_read_order"):
        reader.set_read_order(
            rosbag2_py.ReadOrder(sort_by=rosbag2_py.ReadOrderSortBy.File)
        )

    writer = rosbag2_py.SequentialWriter()
    storage_preset = "fastwrite" if storage_id == "mcap" and args.fastwrite else ""
    writer.open(
        rosbag2_py.StorageOptions(
            uri=str(output_bag),
            storage_id=storage_id,
            storage_preset_profile=storage_preset,
        ),
        converter_options,
    )

    topic_types = {}
    for topic in reader.get_all_topics_and_types():
        topic_types[topic.name] = topic.type
        writer.create_topic(topic)

    message_types = {
        topic: get_message(type_name)
        for topic, type_name in topic_types.items()
        if type_name == "sensor_msgs/msg/Image"
    }

    total = 0
    converted = 0
    while reader.has_next():
        topic, data, timestamp = reader.read_next()
        total += 1
        msg_type = message_types.get(topic)
        if msg_type is None:
            writer.write(topic, data, timestamp)
            continue

        msg = deserialize_message(data, msg_type)
        msg, did_convert = convert_image(msg)
        if did_convert:
            converted += 1
            data = serialize_message(msg)
        writer.write(topic, data, timestamp)

    writer.close()
    reader.close()
    print(f"Input:     {input_bag}")
    print(f"Output:    {output_bag}")
    print(f"Messages:  {total}")
    print(f"Converted: {converted} image messages to bgr8")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(1)
