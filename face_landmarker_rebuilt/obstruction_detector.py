import os
import cv2
import numpy as np

try:
    # Uu tien 1: Dung thu vien LiteRT moi nhat tu Google (ai-edge-litert)
    import ai_edge_litert.interpreter as tflite
except ImportError:
    try:
        # Uu tien 2: Dung tflite_runtime cu
        import tflite_runtime.interpreter as tflite
    except ImportError:
        # Uu tien 3: Bat buoc phai xai Tensorflow full (chi dung khi dev tren Colab/PC)
        import tensorflow.lite as tflite

# Cấu hình đường dẫn model mặc định bên trong file module
DEFAULT_MODEL_PATH = os.path.join("models", "custom_mobilenetv3_v1_float16.tflite")


class ObstructionDetector:
    def __init__(
        self,
        model_path: str = DEFAULT_MODEL_PATH,
        num_threads: int = 4,
        threshold_mask: float = 0.5,
        threshold_sunglasses: float = 0.5,
    ):
        """
        Khoi tao mo hinh TFLite kiem tra vat che (Khau trang / Kinh ram).
        """
        self.model_path = model_path
        self.threshold_mask = threshold_mask
        self.threshold_sunglasses = threshold_sunglasses

        if not os.path.exists(self.model_path):
            raise FileNotFoundError(
                f"[ERROR] Khong tim thay file model tai: '{self.model_path}'. "
                f"Vui long kiem tra lai thu muc 'models/'."
            )

        self.interpreter = tflite.Interpreter(
            model_path=self.model_path, num_threads=num_threads
        )
        self.interpreter.allocate_tensors()

        self.input_details = self.interpreter.get_input_details()
        self.output_details = self.interpreter.get_output_details()

        self.input_height = self.input_details[0]["shape"][1]
        self.input_width = self.input_details[0]["shape"][2]

    def preprocess(self, face_crop: np.ndarray) -> np.ndarray:
        """
        Tien xuly anh khuon mat (BGR -> RGB -> Resize -> Batch dimension).
        """
        rgb_crop = cv2.cvtColor(face_crop, cv2.COLOR_BGR2RGB)
        resized_crop = cv2.resize(rgb_crop, (self.input_width, self.input_height))
        input_data = np.expand_dims(resized_crop, axis=0).astype(np.float32)
        return input_data

    def predict(self, face_crop: np.ndarray) -> dict:
        """
        Du doan khau trang / kinh ram tu anh crop khuon mat (BGR).
        """
        if face_crop is None or face_crop.size == 0:
            return {
                "has_mask": False,
                "has_sunglasses": False,
                "is_obstructed": False,
                "prob_mask": 0.0,
                "prob_sunglasses": 0.0,
            }

        input_data = self.preprocess(face_crop)

        self.interpreter.set_tensor(self.input_details[0]["index"], input_data)
        self.interpreter.invoke()

        output_data = self.interpreter.get_tensor(self.output_details[0]["index"])[0]

        prob_mask = float(output_data[0])
        prob_sunglasses = float(output_data[1])

        has_mask = prob_mask >= self.threshold_mask
        has_sunglasses = prob_sunglasses >= self.threshold_sunglasses
        is_obstructed = has_mask or has_sunglasses

        return {
            "has_mask": has_mask,
            "has_sunglasses": has_sunglasses,
            "is_obstructed": is_obstructed,
            "prob_mask": prob_mask,
            "prob_sunglasses": prob_sunglasses,
        }