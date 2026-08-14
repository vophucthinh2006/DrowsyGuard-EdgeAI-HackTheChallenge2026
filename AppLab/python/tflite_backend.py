"""
Loads the `Interpreter` class used to run .tflite files, trying backends in
priority order (whichever package is available/installable on your machine
gets used):

  1. ai_edge_litert   (newest package from Google, successor to
                        tflite-runtime, but not every machine/Python version
                        has a prebuilt wheel for it yet)
  2. tflite_runtime    (older, lightweight package, wheels available for
                        most Windows/Linux/Mac/ARM targets)
  3. tensorflow.lite   (if you already have full TensorFlow installed, this
                        is guaranteed to work)

You only need ONE of the three packages installed - no need to install all
three.
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
                "No TFLite backend found. Please install ONE of the following:\n"
                "  pip install ai-edge-litert\n"
                "  pip install tflite-runtime\n"
                "  pip install tensorflow\n"
                "Per-package error details:\n  - " + "\n  - ".join(_ERRORS)
            )

print(f"[tflite_backend] Using backend: {_BACKEND}")
