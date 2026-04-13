# nohcam-tracker 実装計画

## 概要

MediaPipe を使用してウェブカメラから Face + Hands + Pose 検出し、JSONL 形式で stdout に出力する Python アプリケーション。

- ウェブカメラ入力: 1920x1080 @ 30FPS
- 検出: MediaPipe Holistic (Hands 21x2 + Pose 33 landmarks + Face 468 landmarks)
- 処理解像度: 640x480 (パフォーマンス優先)
- 出力座標: 正規化座標 (0.0-1.0)
- 出力形式: JSONL (1フレーム=1行)

## 技术スタック

- Python 3.11+
- uv (パッケージ管理)
- OpenCV (カメラ入力)
- MediaPipe Tasks (Holistic Landmarker)

## 依存関係

```
opencv-python>=4.8.0
mediapipe>=0.10.0
```

## ディレクトリ構成

```
nohcam-tracker/
├── pyproject.toml
├── models/
│   └── holistic_landmarker.task
├── src/
│   └── nohcam_tracker/
│       ├── __init__.py
│       ├── __main__.py
│       └── tracker.py
└── README.md
```

## 実装タスク

- [x] docs/plan-python.md 作成
- [x] uv プロジェクト初期化
- [x] pyproject.toml 設定
- [x] 依存インストール (opencv-python, mediapipe)
- [x] tracker.py 実装 (カメラキャプチャ、MediaPipe Holistic)
- [x] __main__.py 実装 (エントリポイント、JSONL出力)
- [x] モデルダウンロード (holistic_landmarker.task)
- [ ] パフォーマンステスト (FPS測定)
- [ ] Face 検出対応 (HolisticTasks API ではデフォルトで face mesh なし)

## JSONL 出力フォーマット

```json
{"frame":1,"timestamp_ms":504,"face":[],"hands":[{"handedness":"Left","landmarks":[{"x":0.582,"y":0.803,"z":0.0},...]},...],"pose":[{"x":0.489,"y":0.322,"z":-0.333},...]}
```

フィールド説明:
- `frame`: フレーム番号
- `timestamp_ms`: タイムスタンプ (ミリ秒)
- `face`: Face landmarks (468点, 正規化座標 x,y,z) - *現在空*
- `hands`: Hand landmarks (21点x2, handedness 含む)
- `pose`: Pose landmarks (33点, 正規化座標 x,y,z)

## パフォーマンス

- テスト結果: 数FPS程度 (フレームによる)
- GPUなしWindowsでは30fps全フレーム処理は困難な場合あり

## C++ 連携

- CreateProcess で子プロセス起動
- stdout リダイレクトでJSONL読み取り
- C++側でLive2Dパラメータに変換・補間

## 実行コマンド

```powershell
uv run nohcam-tracker
# または
python -m nohcam_tracker
```

## 確認項目

- [x] カメラ Captur 動作確認
- [ ] Face 検出精度 (HolisticTasks API ではデフォルトで face mesh なし - 別途FaceLandmarker追加が必要)
- [x] Hands 検出精度
- [x] Pose 検出精度
- [x] JSONL出力フォーマット確認
- [ ] FPS 測定 (要テスト)
- [ ] C++ 子プロセス連携確認