"""
Nạp class `Interpreter` để chạy file .tflite, thử lần lượt các gói theo thứ
tự ưu tiên (gói nào có sẵn/ cài được trên máy bạn thì dùng gói đó):

  1. ai_edge_litert   (gói mới nhất, kế thừa tflite-runtime, nhưng không phải
                        máy/Python nào cũng có sẵn wheel)
  2. tflite_runtime    (gói cũ hơn, nhẹ, nhiều wheel cho Windows/Linux/Mac)
  3. tensorflow.lite   (nếu bạn đã cài tensorflow đầy đủ thì chắc chắn có)

Chỉ cần cài MỘT trong ba gói trên là chạy được, không cần cài đủ cả ba.
"""

_ERRORS = []

try:
    from ai_edge_litert.interpreter import Interpreter  # noqa: F401
    _BACKEND = "ai_edge_litert"
except Exception as e:  # pragma: no cover
    _ERRORS.append(f"ai_edge_litert: {e}")
    try:
        from tflite_runtime.interpreter import Interpreter  # noqa: F401
        _BACKEND = "tflite_runtime"
    except Exception as e2:  # pragma: no cover
        _ERRORS.append(f"tflite_runtime: {e2}")
        try:
            from tensorflow.lite import Interpreter  # noqa: F401
            _BACKEND = "tensorflow.lite"
        except Exception as e3:  # pragma: no cover
            _ERRORS.append(f"tensorflow: {e3}")
            raise ModuleNotFoundError(
                "Khong tim thay backend TFLite nao. Hay cai MOT trong cac lenh sau:\n"
                "  pip install ai-edge-litert\n"
                "  pip install tflite-runtime\n"
                "  pip install tensorflow\n"
                "Chi tiet loi tung goi:\n  - " + "\n  - ".join(_ERRORS)
            )

print(f"[tflite_backend] Dang dung backend: {_BACKEND}")
