# mediapipe-vmc-converter

MediaPipe Tasks API の Pose 検出結果を VMC プロトコル OSC に変換して送信する Python アプリです。

## セットアップ

```powershell
cd mediapipe-vmc-converter
uv sync --extra dev
```

## 実行

```powershell
uv run mediapipe-vmc-converter --target vseeface --camera 1
```

### 主なオプション

- `--config`: 設定ファイルパス（デフォルト: `config.json`）
- `--target`: `vseeface` / `warudo` / `vnyan`
- `--camera`: OpenCV カメラインデックス
- `--model`: PoseLandmarker の `.task` モデルパス
- `--enable-face`: 表情処理を有効化（初期版はプレースホルダ）
- `--enable-hands`: 手トラッキング処理を有効化（初期版はプレースホルダ）

## モデル配置

- 既定のモデルパスは `../assets/mediapipe/pose_landmarker_full.task`
- 存在しない場合は `--model` で明示指定してください

## テスト

```powershell
uv run pytest
```

## 既知制約

- 初期版は Pose 中心実装です（Face/Hands はフラグと骨組みのみ）
- カメラ環境によって FPS が 30 未満になる場合があります
- MediaPipe モデルと OpenCV カメラアクセスが必要です
