# NohCam Python 開発計画

## 概要

Python で MediaPipe を使用してリアルタイム顔・手足・姿勢検出を行い、Live2D モデルを動かして画面上に表示する。

```
Webcam → MediaPipe (顔手足姿勢検出) → Live2D パラメータマッピング → Pygame + live2d-py (描画)
```

## 技術スタック

| コンポーネント | ライブラリ | 備考 |
|-----------|-----------|------|
| 映像入力 | OpenCV (cv2) | Webcam からフレーム取得 |
| トラッキング | MediaPipe | 顔・手足・姿勢検出 |
| Live2D 描画 | live2d-py (live2d.v3) | Cubism 3.0+ モデル対応 |
| ウィンドウ | Pygame + OpenGL | OpenGL コンテキスト必要 |
| 仮想カメラ | pyvirtualcam | OBS Virtual Camera に出力 |

## 依存ライブラリ

```bash
pip install opencv-python mediapipe pygame pyopengl pyvirtualcam live2d-py
```

## ディレクトリ構成

```
nohcam-tracker/src/nohcam_tracker/
├── __init__.py
├── __main__.py          # エントリポイント
├── tracker.py           # MediaPipe トラッキング (既存)
├── live2d_model.py    # Live2D モデル管理
├── parameter_mapper.py  # MediaPipe → Live2D パラメータ変換
└── app.py            # Pygame + メインループ (新規)
```

## 実装ステップ

- [ ] Step 1: 環境構築 - 依存ライブラリをインストール (uv sync)
- [ ] Step 2: Live2D モデル描画 - Pygame + OpenGL ウィンドウ作成、モデル読み込み・描画
- [ ] Step 3: トラッキング統合 - tracker.py の TrackingResult を live2d_model.py に連携
- [ ] Step 4: パラメータマッピング - MediaPipe のランドマークを Live2D パラメータに変換
- [ ] Step 5: 仮想カメラ出力 (オプション) - pyvirtualcam で OBS Virtual Camera に出力

## Live2D パラメータマッピング

### 顔 (Face)

| MediaPipe | Live2D パラメータ | 説明 |
|----------|------------------|------|
| 鼻 (landmark 1) | ParamAngleX | 顔 横方向回転 |
| - | ParamAngleY | 顔 縦方向回転 |
| - | ParamAngleZ | 顔 傾き |

### 左手 (Left Hand)

| MediaPipe | Live2D パラメータ |
|----------|------------------|
| 手首 (landmark 0) | ParamHandL_X, ParamHandL_Y |

### 右手 (Right Hand)

| MediaPipe | Live2D パラメータ |
|----------|------------------|
| 手首 (landmark 0) | ParamHandR_X, ParamHandR_Y |

## モデルファイルの場所

Live2D モデルは `assets\live2d-models\` ディレクトリに配置済み。

```
assets/live2d-models/
├── hiyori_free_jp/   #  無料版モデル
├── miku/            #  Hatsune Miku モデル
```

## 注意事項

1. live2d-py は Windows + Python 3.2+ 要
2. OpenGL コンテキストが必要
3. モデルは Cubism 3.0+ (model3.json)
4. パラメータ ID はモデルごとに異なる

## TODO

- [ ] Step 1: 依存ライブラリインストール
- [ ] Step 2: Live2D 描画確認
- [ ] Step 3: トラッキング統合
- [ ] Step 4: パラメータマッピング実装