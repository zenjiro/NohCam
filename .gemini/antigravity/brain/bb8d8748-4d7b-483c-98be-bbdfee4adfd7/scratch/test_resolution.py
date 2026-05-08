import os
import sys

# Add src to path
sys.path.append(os.path.abspath("src"))

from nohcam.__main__ import resolve_model_path

test_cases = [
    "ulvm2_0001",
    "ulvm2_0001.model3.json",
    "assets/live2d-models/ulvm2_0001/ulvm2_0001.model3.json",
    "A1",
    "nonexistent"
]

for tc in test_cases:
    resolved = resolve_model_path(tc)
    print(f"Input: {tc} -> Resolved: {resolved}")
