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

- [x] Step 1: 環境構築 - 依存ライブラリをインストール (uv sync)
- [x] Step 2: Live2D モデル描画 - Pygame + OpenGL ウィンドウ作成、モデル読み込み・描画
- [x] Step 3: トラッキング統合 - tracker.py の TrackingResult を live2d_model.py に連携
- [x] Step 4: パラメータマッピング - MediaPipe のランドマークを Live2D パラメータに変換
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

## モデル別パラメータ情報

### パラメータ早見表

| モデル | 総数 | Angle パラメータ | Arm パラメータ | 備考 |
|--------|------|------------------|---------------|------|
| hiyori_free_jp | 29 | ParamAngleX/Y/Z | ParamArmLA, ParamArmRA | シンプル |
| miku | 30 | PARAM_ANGLE_X/Y/Z | PARAM_ARM_L/R | 大文字 |
| mao_pro_jp | 128 | ParamAngleX/Y/Z | ParamArmLA01/RA01, ParamHandLA/RA | **一番细致** |
| shizuku | 45 | PARAM_ANGLE_X/Y/Z | PARAM_ARM_L/R, PARAM_ARM_L_02/R_02 | 複数腕 |
| hibiki | 25 | PARAM_ANGLE_X/Y/Z | PARAM_ARM_R | 左腕なし |

### 詳細

**mao_pro_jp** (腕の詳細一段)
```
ParamArmLA01  # 左腕01 (肩〜肘)
ParamArmLA02  # 左腕02 (肘〜手首)
ParamArmLA03  # 左腕03
ParamHandLA   # 左手
ParamArmRA01  # 右腕01
ParamArmRA02  # 右腕02
ParamArmRA03  # 右腕03
ParamHandRA   # 右手
```

**shizuku** (手の詳細一段)
```
PARAM_ARM_L, PARAM_ARM_R                     # 腕
PARAM_ARM_L_02, PARAM_ARM_R_02              # 腕02
PARAM_HAND_L, PARAM_HAND_R                  # 左手, 右手
PARAM_HAND_02_L, PARAM_HAND_02_R            # 左手02, 右手02
```

**hiyori_free_jp** (シンプル)
```
ParamArmLA   # 左腕
ParamArmRA   # 右腕
```

### 結論

腕・手の動き**: mao_pro_jp > shizuku > hiyori/miku > hibiki**

mao_pro_jp は128個のパラメータがあり、腕を複数セグメントに分割して制御可能。

## モデルファイルの場所

Live2D モデルは `assets\live2d-models\` ディレクトリに配置済み。

```
assets/live2d-models/
├── hiyori_free_jp/   # 29	params, シンプル
├── miku/             # 30	params, 大文字
├── mao_pro_jp/       # 128	params, 一番细致 (推奨)
├── shizuku/          # 45	params, 二段腕
└── hibiki/          # 25	params, 左腕なし
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