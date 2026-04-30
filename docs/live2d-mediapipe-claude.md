# Live2D × MediaPipe: Pythonで姿勢推定結果をアバターに反映する方法

調査日: 2026-04-30

---

## 概要

Live2D アバターを MediaPipe の姿勢推定・顔推定結果で動かす方法として、**`live2d-py` による Python 直接制御**が現実的な選択肢となる。

---

## `live2d-py` で Python から直接制御

### 概要

[`live2d-py`](https://github.com/EasyLive2D/live2d-py) は Live2D Cubism SDK の Python C 拡張ラッパー。  
**MediaPipe との統合サンプル (`main_facial_bind_mediapipe.py`) が公式に含まれている**ため最も手軽。

### インストール

```bash
pip install live2d-py mediapipe opencv-python
```

### 基本的な構造

```python
import cv2
import mediapipe as mp
import live2d.v3 as live2d

# --- 初期化 ---
live2d.init()
model = live2d.LAppModel()
model.LoadModelJson("path/to/model.model3.json")

mp_face_mesh = mp.solutions.face_mesh.FaceMesh(
    max_num_faces=1,
    refine_landmarks=True,
    min_detection_confidence=0.5,
    min_tracking_confidence=0.5,
)

cap = cv2.VideoCapture(0)

while True:
    ret, frame = cap.read()
    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    results = mp_face_mesh.process(rgb)

    if results.multi_face_landmarks:
        lm = results.multi_face_landmarks[0].landmark

        # 顔の向き (頭部回転) をパラメータに変換
        # ランドマーク番号は MediaPipe FaceMesh 仕様に基づく
        nose_tip = lm[4]
        left_eye = lm[33]
        right_eye = lm[263]

        # 左右回転 (ParamAngleX)
        angle_x = (right_eye.x - left_eye.x) * 60 - 30  # 要調整
        # 上下回転 (ParamAngleY)
        angle_y = (0.5 - nose_tip.y) * 60

        model.SetParameterValue("ParamAngleX", angle_x)
        model.SetParameterValue("ParamAngleY", angle_y)

        # 口の開き (ParamMouthOpenY)
        upper_lip = lm[13]
        lower_lip = lm[14]
        mouth_open = abs(lower_lip.y - upper_lip.y) * 100
        model.SetParameterValue("ParamMouthOpenY", min(mouth_open, 1.0))

    model.Update()
    # レンダリング処理 (OpenGL or PyGame 等と組み合わせる)
```

### 注意点

- `live2d-py` のレンダリングには **OpenGL コンテキスト** が必要（PyGame や GLFW と組み合わせる）
- モデルが Cubism 3.x / 4.x 形式であることを確認すること
- パラメータ名 (`ParamAngleX` など) はモデルごとに異なる場合がある

---

## 参考プロジェクト

| プロジェクト | 説明 |
|---|---|
| [live2d-py](https://github.com/EasyLive2D/live2d-py) | Python 用 Live2D ライブラリ。MediaPipe サンプル付き |
| [mediapipe-osc](https://github.com/cansik/mediapipe-osc) | MediaPipe 結果を OSC で送信するサンプル集（参考） |

---

## 推奨構成まとめ

```
Webカメラ
  ↓
MediaPipe (Python)
  FaceMesh / Holistic / Pose
  ↓
パラメータ変換 (角度計算, 正規化)
  ↓
live2d-py → OpenGL レンダリング  ← Python 完結
```

**`live2d-py` を使った Python 単体構成が現時点での推奨**。

---

## Sources

- [live2d-py (PyPI)](https://pypi.org/project/live2d-py/)
- [EasyLive2D/live2d-py (GitHub)](https://github.com/EasyLive2D/live2d-py)
- [cansik/mediapipe-osc (GitHub)](https://github.com/cansik/mediapipe-osc)
- [MediaPipe Pose ドキュメント](https://github.com/google-ai-edge/mediapipe/blob/master/docs/solutions/pose.md)
