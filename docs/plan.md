# NohCam — 実装詳細計画

## 基本方針

- **本線は GUI 非依存** にする
  - Webカメラ入力 → トラッキング → Live2D反映 → 仮想カメラ出力 を独立した処理パイプラインとして構築する
- **GUI は確認と設定に限定** する
  - リアルタイム描画は必須ではなく、低FPS・低解像度の確認用プレビューで十分
- **プレビューは間引き表示** とする
  - 例: 320x180 〜 640x360, 5〜15fps
  - 仮想カメラ本線のフレームレートや解像度に GUI を引きずられない設計を優先する
- **本番UIは WinUI 3 を採用** する
  - ユーザー向け UI は WinUI 3 で構築する

---

## ディレクトリ構成

```
NohCam/
├── CMakeLists.txt
├── vcpkg.json                        # 依存パッケージ定義
├── README.md
│
├── src/
│   ├── main.cpp                      # エントリーポイント
│   │
│   ├── app/
│   │   ├── Application.h/.cpp        # アプリ全体のライフサイクル管理
│   │   └── Config.h/.cpp             # 設定の読み書き (JSON)
│   │
│   ├── capture/
│   │   ├── CameraCapture.h/.cpp      # Media Foundation でWebカメラ入力
│   │   └── FrameBuffer.h/.cpp        # フレームのスレッドセーフなキュー
│   │
│   ├── pipeline/
│   │   ├── ProcessingPipeline.h/.cpp # GUI非依存の処理本線
│   │   └── PreviewTap.h/.cpp         # 確認用プレビューへの間引き出力
│   │
│   ├── tracking/
│   │   ├── Tracker.h/.cpp            # MediaPipe の統括ラッパー
│   │   ├── FaceTracker.h/.cpp        # 顔ランドマーク・向き・Blendshape
│   │   ├── HandTracker.h/.cpp        # 手のランドマーク
│   │   └── TrackingResult.h          # トラッキング結果の共通データ構造体
│   │
│   ├── avatar/
│   │   ├── Live2DManager.h/.cpp      # Cubism SDK 初期化・モデル管理
│   │   ├── AvatarModel.h/.cpp        # モデルのロード・パラメータ更新
│   │   └── ParameterMapper.h/.cpp    # TrackingResult → Cubism パラメータ変換
│   │
│   ├── render/
│   │   ├── D3D11Renderer.h/.cpp      # アバター生成用 DirectX 11 レンダラー
│   │   ├── RenderTarget.h/.cpp       # オフスクリーンレンダリング用テクスチャ
│   │   └── FrameExporter.h/.cpp      # 仮想カメラ出力用フレーム書き出し
│   │
│   ├── virtualcam/
│   │   ├── VirtualCamFilter.h/.cpp   # DirectShow フィルタ本体 (COM実装)
│   │   ├── VirtualCamPin.h/.cpp      # 出力ピン (IPin / IMemInputPin 実装)
│   │   ├── VirtualCamServer.h/.cpp   # 共有メモリ経由でフレームを渡す仕組み
│   │   └── Register.cpp              # DLL登録用 DllRegisterServer / DllUnregisterServer
│   │
│   └── ui/
│       ├── MainWindow.h/.cpp         # WinUI 3 / Win32 ホスト初期化
│       ├── SettingsViewModel.h/.cpp  # UI設定状態
│       ├── PreviewSurface.h/.cpp     # 確認用プレビューの表示面
│       └── panels/
│           ├── PreviewPanel.h/.cpp   # 低FPS・低解像度の確認用プレビュー
│           ├── TrackingPanel.h/.cpp  # 感度・オフセット調整
│           ├── AvatarPanel.h/.cpp    # モデルファイル選択・パーツ設定
│           └── CameraPanel.h/.cpp    # カメラデバイス選択・入力設定
│
├── driver/                           # DirectShow Filter DLL (別ビルドターゲット)
│   ├── CMakeLists.txt
│   └── NohCamVirtualCamera.def       # DLLエクスポート定義
│
├── assets/
│   └── models/                       # デフォルトのLive2Dモデル一式
│
└── third_party/
    ├── mediapipe/
    └── CubismSdkForNative/
```

---

## フェーズ別実装計画

### Phase 1 — 土台構築 (1〜2週間)

#### 1-1. ビルド環境セットアップ
`✅ 完了`

- Visual Studio Community 2026
  - C++ によるデスクトップ開発ワークロード
  - MSVC 19.50.35728
- CMake 4.2.3
- Git
- Python 3.11.13 (uv 経由)
- Bazelisk / Bazel 9.0.1
- Cubism SDK for Native 5-r.5
- vcpkg 2026-03-04
- GPU は当面 CPU 推論で進める
- CMake 設定成功
- Cubism Framework 静的ライブラリのビルド成功
- Win32 + DX11 のビルド確認済み

残作業:
- MediaPipe を公式ビルド手順に従い静的ライブラリとしてビルド

#### 1-2. Win32 ウィンドウ + DirectX 11 初期化
`✅ 完了`

- HWND 生成
- `WM_SIZE` / `WM_DESTROY` ハンドリング
- `IDXGISwapChain1` + `ID3D11Device` + `ID3D11DeviceContext` 初期化
- WinUI 3 ホストと接続できるレンダリング基盤を整備する

#### 1-3. Media Foundation でカメラ入力
`🟡 進行中`

- `IMFSourceReader` を使い Webカメラから RGB32 / NV12 フレームを取得
- GUI表示用には低解像度・低FPSの確認用プレビューを別系統で生成
- GUIプレビューは本線を間引いたフレームを表示するだけに留める
- カメラ入力が安定して取得できることを確認する
- GUIプレビューを閉じても本線が成立する構造にする

#### 1-4. ビルド手順
`✅ 完了`

- vcpkg 依存関係インストール手順
- CMake 設定・ビルド手順
- 実行方法

---

### Phase 2 — トラッキング (2〜3週間)

#### 2-1. MediaPipe 顔ランドマーク
`⚪ 未着手`

- FaceLandmarker タスクAPIを使用
- 468点ランドマーク取得
- 52種の Blendshape 係数取得
- `facial_transformation_matrix` で顔の6DoF姿勢取得
  - Yaw
  - Pitch
  - Roll

#### 2-2. MediaPipe 手ランドマーク
`⚪ 未着手`

- HandLandmarker タスクAPIを使用
- 左右各21点のランドマーク取得
- 手首・指の関節角度を算出して `TrackingResult` に格納

#### 2-3. TrackingResult 構造体設計
`⚪ 未着手`

```cpp
struct FaceResult {
    float yaw, pitch, roll;            // 顔の向き (ラジアン)
    float x, y;                        // 顔の位置 (正規化)
    std::array<float, 52> blendshapes; // 表情係数
};

struct HandResult {
    bool detected;
    std::array<glm::vec3, 21> landmarks;
};

struct TrackingResult {
    FaceResult face;
    HandResult leftHand;
    HandResult rightHand;
    std::chrono::steady_clock::time_point timestamp;
};
```

#### 2-4. スレッド設計
`⚪ 未着手`

```text
[カメラスレッド]  Media Foundation → FrameBuffer
↓
[処理本線]  Tracking / Avatar / VirtualCam 用データ更新
↓
[仮想カメラ出力]  安定したフレーム供給

(別系統)
[PreviewTap]  本線からフレームを間引き
↓
[GUIスレッド]  低FPS / 低解像度プレビュー表示
```

- GUIプレビューは本線の成否を左右しない
- 本線とGUIの更新頻度は分離する
- 必要であればプレビューを完全無効化できる構造にする

---

### Phase 3 — Live2D アバター制御 (2〜3週間)

#### 3-1. Cubism SDK 初期化
`⚪ 未着手`

- `CubismFramework::StartUp()` → `Initialize()`
- `model3.json` からモデルをロード
- DX11 レンダラー (`CubismRenderer_D3D11`) をアタッチ

#### 3-2. ParameterMapper の設計
`⚪ 未着手`

TrackingResult の各値を Cubism のパラメータIDにマッピングする変換層

| TrackingResult              | Cubism パラメータID           | 変換処理             |
|----------------------------|------------------------------|----------------------|
| face.yaw                   | ParamAngleX                  | ラジアン→[-30, 30]  |
| face.pitch                 | ParamAngleY                  | ラジアン→[-30, 30]  |
| face.roll                  | ParamAngleZ                  | ラジアン→[-30, 30]  |
| face.x, face.y             | ParamBodyAngleX / BodyAngleY | 顔位置→体の揺れ     |
| blendshapes[eyeBlinkLeft]  | ParamEyeLOpen                | 反転・スケール       |
| blendshapes[eyeBlinkRight] | ParamEyeROpen                | 反転・スケール       |
| blendshapes[jawOpen]       | ParamMouthOpenY              | スケール             |
| blendshapes[mouthSmileLeft]| ParamMouthForm               | 左右平均             |
| handLandmarks              | ParamArmL / ParamArmR        | 独自計算             |

- 各パラメータにスムージングをかけてぎこちなさを除去する

#### 3-3. オフスクリーンレンダリング
`⚪ 未着手`

- Live2D を `ID3D11Texture2D (RGBA)` にオフスクリーン描画
- 仮想カメラ出力へ流すフレームをここから供給
- GUIには同じテクスチャを高頻度表示せず、必要なら縮小・間引きした確認用プレビューのみ渡す

---

### Phase 4 — 仮想カメラ (2〜3週間)

#### 4-1. DirectShow フィルタ設計
`⚪ 未着手`

- COM実装: `IBaseFilter` / `IPin` / `IAMStreamConfig`
- 別DLLとしてビルド: `NohCamVirtualCamera.dll`

#### 4-2. フレーム共有方式
`⚪ 未着手`

```text
NohCam本体 (EXE) ↔ DirectShowフィルタ (DLL) 間のフレーム受け渡し
EXE側 (FrameExporter)
DX11 RenderTarget → 仮想カメラ用フレーム → 名前付き共有メモリ (CreateFileMapping)
→ セマフォで新フレームを通知
DLL側 (VirtualCamPin)
セマフォ待機 → 共有メモリ読み出し → IMemInputPin::Receive でZoomへ渡す
```

- GUIプレビュー用の縮小画像とは別経路にする
- 仮想カメラ出力を最優先し、GUI描画負荷が出力品質へ影響しないようにする

#### 4-3. DLL 登録
`⚪ 未着手`

- インストーラーまたは初回起動時に `regsvr32` 相当の処理を実行
- `HKCR\CLSID\{NohCam-GUID}` にフィルタ情報を登録
- Teams / Zoom 側のカメラ一覧に `NohCam Virtual Camera` と表示されるようにする

---

### Phase 5 — UI 仕上げ・設定 (1週間)

#### WinUI 3 ベースの設定UI
`⚪ 未着手`

```text
┌─────────────────────────────────────────────┐
│  NohCam                          [最小化][✕] │
├───────────────┬─────────────────────────────┤
│               │  Camera                     │
│  [確認用      │  デバイス: [USB Camera  ▼]  │
│   プレビュー] │  入力:     [1280x720   ▼]   │
│  640x360      ├─────────────────────────────┤
│  10fps        │  Avatar                     │
│               │  モデル: [Select .moc3...]  │
│               ├─────────────────────────────┤
│               │  Tracking                   │
│               │  顔感度  ━━●━━━━  0.8      │
│               │  口スケール ━●━━━━  0.6    │
│               │  手トラッキング [ON]        │
│               ├─────────────────────────────┤
│               │  Output                     │
│               │  仮想カメラ [● 配信中]      │
│               │  出力: 1280x720 @ 30fps     │
└───────────────┴─────────────────────────────┘
```

- 左側は確認用プレビューのみ
- 低FPS・低解像度表示を前提とする
- UIは状態確認と設定変更に専念し、本線の高頻度レンダリングは担わない

#### 設定の永続化
`⚪ 未着手`

- `config.json` に保存
  - カメラデバイスID
  - モデルファイルパス
  - 各パラメータの感度・オフセット
  - 出力解像度・FPS
  - プレビュー有効/無効
  - プレビュー解像度 / FPS

---

### Phase 6 — 品質・パフォーマンス (1週間)

#### パフォーマンス目標
`⚪ 未着手`

| 処理               | 目標          |
|-------------------|---------------|
| トラッキング推論   | < 10ms / frame (GPU推論) |
| Live2D 描画        | < 5ms / frame |
| 仮想カメラ遅延     | < 100ms 総合  |
| CPU使用率          | < 20%         |
| GUIプレビュー      | 5〜15fps, 低負荷 |

#### 最適化ポイント
`⚪ 未着手`

- MediaPipe を GPU デリゲート (CUDA / DirectML) で動かす
- トラッキングは 30fps で十分、GUIプレビューはさらに低頻度でよい
- 共有メモリのゼロコピー化
- GUIプレビュー用の縮小・間引き処理を本線から分離する

#### エラーハンドリング
`⚪ 未着手`

- カメラ切断時の自動再接続
- モデルファイル不正時のフォールバック表示
- DirectShowフィルタ未登録時のガイドダイアログ
- GUI停止時も仮想カメラ本線が継続すること

---

## 開発順序まとめ

- Phase 1 → GUI非依存の入力パイプラインが動く
- Phase 2 → 顔・手トラッキング結果が本線に反映される
- Phase 3 → アバターが顔の動きに追従し、仮想カメラ向けフレームを生成できる
- Phase 4 → Zoomで自分のアバターが映る
- Phase 5 → WinUI 3 ベースの設定UIが整い、他人に使わせられる
- Phase 6 → 軽くて安定する

---

## 主な依存ライブラリ バージョン一覧

| ライブラリ             | バージョン         | 取得方法              |
|------------------------|--------------------|-----------------------|
| MediaPipe              | 0.10.x             | GitHub ソースビルド   |
| Cubism SDK for Native  | 5-r.1              | Live2D 公式サイト     |
| WinUI 3                | Windows App SDK 1.x| NuGet / 公式手順      |
| DirectX 11             | OS同梱             | Windows SDK           |
| nlohmann/json          | 3.11.x             | vcpkg                 |
| spdlog                 | 1.13.x             | vcpkg                 |
| glm                    | 1.0.x              | vcpkg                 |
