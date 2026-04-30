# Live2DアバターをMediaPipe姿勢推定で動かすPython実装ガイド

## 前提
`VTube Studio` は無料版の商用利用制約があるため、このドキュメントでは選択肢から外します。

## 結論
次の構成が実装しやすく、安全です。

1. PythonでWebカメラ入力 + MediaPipe Pose推定
2. Pythonで推定値を整形（平滑化・クランプ）
3. ローカルIPC（UDP/TCP/Named Pipe）でC++アプリへ送信
4. C++側（Cubism SDK Native）で`ParamAngleX/Y/Z`等へ反映

この方法なら、Live2D Cubism SDKのライセンス範囲で完結し、配信ツール依存を避けられます。

## 参考にした一次情報
- MediaPipe Pose Landmarker Python: https://ai.google.dev/edge/mediapipe/solutions/vision/pose_landmarker/python
- Live2D Standard Parameter List（`ParamAngleX`等）: https://docs.live2d.com/en/cubism-editor-manual/standard-parameter-list/
- Live2D Parameter操作（Set/Add）: https://docs.live2d.com/4.2/en/cubism-sdk-manual/parameters/

## アーキテクチャ
- `Python Tracker`:
  - OpenCVでカメラ取得
  - MediaPipe Pose推定
  - `angle_x, angle_y, angle_z, body_x` を計算
  - JSONで `localhost` のUDP/TCPへ送信
- `NohCam C++ Runtime`:
  - 受信した角度値をフレームごとに取り込み
  - Cubismモデルの該当パラメータに`Set`または`Add`で適用
  - 描画

## Python側: 最小サンプル（送信専用）
ファイル例: `tools/mediapipe_pose_sender.py`

```python
import json
import math
import socket
import time

import cv2
import mediapipe as mp

NOSE = 0
LEFT_EAR = 7
RIGHT_EAR = 8
LEFT_SHOULDER = 11
RIGHT_SHOULDER = 12


def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


def smooth(prev: float, cur: float, alpha: float = 0.25) -> float:
    return prev + (cur - prev) * alpha


def norm_to_deg(v: float, scale_deg: float) -> float:
    return clamp(v * scale_deg, -scale_deg, scale_deg)


def compute_pose_params(landmarks):
    nose = landmarks[NOSE]
    le = landmarks[LEFT_EAR]
    re = landmarks[RIGHT_EAR]
    ls = landmarks[LEFT_SHOULDER]
    rs = landmarks[RIGHT_SHOULDER]

    ear_cx = (le.x + re.x) * 0.5
    face_yaw_norm = (nose.x - ear_cx) * 2.0

    sh_cy = (ls.y + rs.y) * 0.5
    face_pitch_norm = (sh_cy - nose.y - 0.15) * 2.0

    dx = rs.x - ls.x
    dy = rs.y - ls.y
    body_roll_deg = clamp(-math.degrees(math.atan2(dy, dx)), -30.0, 30.0)

    angle_x = norm_to_deg(face_yaw_norm, 30.0)
    angle_y = norm_to_deg(face_pitch_norm, 30.0)
    angle_z = body_roll_deg * 0.5
    body_x = angle_x * 0.35
    return angle_x, angle_y, angle_z, body_x


def main():
    BaseOptions = mp.tasks.BaseOptions
    PoseLandmarker = mp.tasks.vision.PoseLandmarker
    PoseLandmarkerOptions = mp.tasks.vision.PoseLandmarkerOptions
    VisionRunningMode = mp.tasks.vision.RunningMode

    options = PoseLandmarkerOptions(
        base_options=BaseOptions(model_asset_path="./pose_landmarker_full.task"),
        running_mode=VisionRunningMode.VIDEO,
        min_pose_detection_confidence=0.5,
        min_pose_presence_confidence=0.5,
        min_tracking_confidence=0.5,
        num_poses=1,
    )

    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        raise RuntimeError("Camera open failed")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = ("127.0.0.1", 5555)

    smoothed = {"x": 0.0, "y": 0.0, "z": 0.0, "bx": 0.0}

    with PoseLandmarker.create_from_options(options) as landmarker:
        while True:
            ok, frame = cap.read()
            if not ok:
                break

            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            mp_img = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
            result = landmarker.detect_for_video(mp_img, int(time.time() * 1000))

            if result.pose_landmarks:
                ax, ay, az, bx = compute_pose_params(result.pose_landmarks[0])
                smoothed["x"] = smooth(smoothed["x"], ax)
                smoothed["y"] = smooth(smoothed["y"], ay)
                smoothed["z"] = smooth(smoothed["z"], az)
                smoothed["bx"] = smooth(smoothed["bx"], bx)

                payload = {
                    "timestamp_ms": int(time.time() * 1000),
                    "face_found": True,
                    "params": {
                        "ParamAngleX": smoothed["x"],
                        "ParamAngleY": smoothed["y"],
                        "ParamAngleZ": smoothed["z"],
                        "ParamBodyAngleX": smoothed["bx"],
                    },
                }
                sock.sendto(json.dumps(payload).encode("utf-8"), target)

            if cv2.waitKey(1) & 0xFF == 27:
                break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
```

## C++側（NohCam）で受けるときの実装ポイント
- 受信スレッドで最新値を`atomic`/mutex保護で保持
- 描画更新スレッドで最新値を読む
- Cubismパラメータ適用順は`Set`→`Add`系の順序に注意
- 推定ロスト時はニュートラル（0）へ緩やかに戻す

## パラメータマッピングの基本
- `ParamAngleX`: 顔の左右
- `ParamAngleY`: 顔の上下
- `ParamAngleZ`: 首/体のロールを少量反映
- `ParamBodyAngleX`: 体のひねり（`ParamAngleX`の低倍率）

## 品質を上げるコツ
- 平滑化（IIR）は必須
- デッドゾーンを設けて微小ノイズを無視
- モデルごとに可動域を再スケーリング（例: ±20〜±45）
- 推論30fps + 描画60fpsでも十分実用

## ハマりやすい点
- `pose_landmarker_full.task` 未配置
- カメラ権限やデバイス競合
- MediaPipe座標系の符号ミス（上下・左右反転）
- モデル側パラメータ範囲と送信値範囲の不一致

## 補足（ライセンス）
利用可否は最終的に各製品の最新利用規約で確認してください。

