"""
Sinh anchor box cho model face_detector.tflite (BlazeFace short-range).

Đây chính là thuật toán "SsdAnchorsCalculator" mà MediaPipe dùng bên trong
graph gốc (được cấu hình trong face_detection_short_range.pbtxt). Khi ta
chạy .task bundle qua Tasks API, bước sinh anchor này bị giấu kín bên trong
C++ graph. Ở đây ta viết lại bằng Python để tự kiểm soát toàn bộ pipeline.

Tham số dưới đây lấy đúng theo config gốc của face_detection_short_range:
  num_layers = 4
  min_scale = 0.1484375
  max_scale = 0.75
  input_size = 128x128
  strides = [8, 16, 16, 16]
  aspect_ratios = [1.0]
  fixed_anchor_size = True
Kết quả: đúng 896 anchor, khớp với output shape [1, 896, 16] / [1, 896, 1]
của model.
"""
import math
import numpy as np


def _calculate_scale(min_scale, max_scale, stride_index, num_strides):
    if num_strides == 1:
        return (min_scale + max_scale) * 0.5
    return min_scale + (max_scale - min_scale) * stride_index / (num_strides - 1)


def generate_anchors(
    num_layers=4,
    min_scale=0.1484375,
    max_scale=0.75,
    input_size_height=128,
    input_size_width=128,
    anchor_offset_x=0.5,
    anchor_offset_y=0.5,
    strides=(8, 16, 16, 16),
    aspect_ratios=(1.0,),
    reduce_boxes_in_lowest_layer=False,
    interpolated_scale_aspect_ratio=1.0,
    fixed_anchor_size=True,
):
    anchors = []
    layer_id = 0
    while layer_id < num_layers:
        anchor_height, anchor_width, aspect_ratios_l, scales = [], [], [], []

        last_same_stride_layer = layer_id
        while (
            last_same_stride_layer < num_layers
            and strides[last_same_stride_layer] == strides[layer_id]
        ):
            scale = _calculate_scale(min_scale, max_scale, last_same_stride_layer, num_layers)
            if last_same_stride_layer == 0 and reduce_boxes_in_lowest_layer:
                aspect_ratios_l += [1.0, 2.0, 0.5]
                scales += [0.1, scale, scale]
            else:
                for ar in aspect_ratios:
                    aspect_ratios_l.append(ar)
                    scales.append(scale)
                if interpolated_scale_aspect_ratio > 0:
                    scale_next = (
                        1.0
                        if last_same_stride_layer == num_layers - 1
                        else _calculate_scale(min_scale, max_scale, last_same_stride_layer + 1, num_layers)
                    )
                    scales.append(math.sqrt(scale * scale_next))
                    aspect_ratios_l.append(interpolated_scale_aspect_ratio)
            last_same_stride_layer += 1

        for i in range(len(aspect_ratios_l)):
            ratio_sqrt = math.sqrt(aspect_ratios_l[i])
            anchor_height.append(scales[i] / ratio_sqrt)
            anchor_width.append(scales[i] * ratio_sqrt)

        stride = strides[layer_id]
        feature_map_height = math.ceil(input_size_height / stride)
        feature_map_width = math.ceil(input_size_width / stride)

        for y in range(feature_map_height):
            for x in range(feature_map_width):
                for anchor_id in range(len(anchor_height)):
                    x_center = (x + anchor_offset_x) / feature_map_width
                    y_center = (y + anchor_offset_y) / feature_map_height
                    if fixed_anchor_size:
                        anchors.append([x_center, y_center, 1.0, 1.0])
                    else:
                        anchors.append(
                            [x_center, y_center, anchor_width[anchor_id], anchor_height[anchor_id]]
                        )

        layer_id = last_same_stride_layer

    return np.array(anchors, dtype=np.float32)


if __name__ == "__main__":
    a = generate_anchors()
    print("So luong anchor sinh ra:", a.shape)  # ky vong (896, 4)
