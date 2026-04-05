# NohCam — 実装詳細計画

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
│   │   ├── D3D11Renderer.h/.cpp      # DirectX 11 デバイス・スワップチェーン
│   │   ├── RenderTarget.h/.cpp       # オフスクリーンレンダリング用テクスチャ
│   │   └── FrameExporter.h/.cpp      # RenderTarget → CPU メモリへの読み戻し
│   │
│   ├── virtualcam/
│   │   ├── VirtualCamFilter.h/.cpp   # DirectShow フィルタ本体 (COM実装)
│   │   ├── VirtualCamPin.h/.cpp      # 出力ピン (IPin / IMemInputPin 実装)
│   │   ├── VirtualCamServer.h/.cpp   # 共有メモリ経由でフレームを渡す仕組み
│   │   └── Register.cpp              # DLL登録用 DllRegisterServer / DllUnregisterServer
│   │
│   └── ui/
│       ├── MainWindow.h/.cpp         # Win32 HWND 生成・メッセージループ
│       ├── ImGuiLayer.h/.cpp         # Dear ImGui 初期化・フレーム管理
│       └── panels/
│           ├── PreviewPanel.h/.cpp   # アバタープレビュー表示
│           ├── TrackingPanel.h/.cpp  # 感度・オフセット調整スライダー
│           ├── AvatarPanel.h/.cpp    # モデルファイル選択・パーツ設定
│           └── CameraPanel.h/.cpp    # カメラデバイス選択・解像度設定
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
    ├── CubismSdkForNative/
    └── imgui/
```

---

## フェーズ別実装計画

### Phase 1 — 土台構築 (1〜2週間)

#### 1-1. ビルド環境セットアップ
- Visual Studio 2022 + CMake + vcpkg の構成
- vcpkg.json に以下を追加
  - nlohmann-json    (設定ファイル読み書き)
  - spdlog           (ロギング)
  - directx-headers  (DX11ヘッダ)
- MediaPipe は公式ビルド手順に従い静的ライブラリとしてビルド
- Cubism SDK は公式ZIPを解凍して third_party/ に配置

#### 1-2. Win32 ウィンドウ + DirectX 11 初期化
- HWND 生成、WM_SIZE / WM_DESTROY ハンドリング
- IDXGISwapChain1 + ID3D11Device + ID3D11DeviceContext の初期化
- Dear ImGui を DX11 バックエンドで組み込み、Hello World UIを表示

#### 1-3. Media Foundation でカメラ入力
- IMFSourceReader を使いWebカメラからNV12/RGB32フレームを取得
- 取得フレームを ID3D11Texture2D にアップロード
- プレビューウィンドウにそのまま表示して動作確認


---

### Phase 2 — トラッキング (2〜3週間)

#### 2-1. MediaPipe 顔ランドマーク
- FaceLandmarker タスクAPIを使用
  - 468点ランドマーク取得
  - 52種の Blendshape 係数取得 (表情)
  - facial_transformation_matrix で顔の6DoF姿勢取得
    - Yaw  (左右の向き)
    - Pitch (上下の向き)
    - Roll (首の傾き)

#### 2-2. MediaPipe 手ランドマーク
- HandLandmarker タスクAPIを使用
  - 左右各21点のランドマーク取得
  - 手首・指の関節角度を算出して TrackingResult に格納

#### 2-3. TrackingResult 構造体設計
```cpp
struct FaceResult {
    float yaw, pitch, roll;           // 顔の向き (ラジアン)
    float x, y;                       // 顔の位置 (正規化)
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

```
[カメラスレッド]  Media Foundation → FrameBuffer (ring buffer)
↓
[トラッキングスレッド]  MediaPipe 推論 → TrackingResult (atomic swap)
↓
[レンダースレッド (メインスレッド)]  結果を読んで Live2D パラメータ更新 → 描画
```

---

### Phase 3 — Live2D アバター制御 (2〜3週間)

#### 3-1. Cubism SDK 初期化
- CubismFramework::StartUp() → Initialize()
- model3.json からモデルをロード
- DX11 レンダラー (CubismRenderer_D3D11) をアタッチ

#### 3-2. ParameterMapper の設計
TrackingResult の各値を Cubism のパラメータIDにマッピングする変換層

| TrackingResult              | Cubism パラメータID          | 変換処理              |
|----------------------------|-----------------------------|-----------------------|
| face.yaw                   | ParamAngleX                 | ラジアン→[-30, 30]   |
| face.pitch                 | ParamAngleY                 | ラジアン→[-30, 30]   |
| face.roll                  | ParamAngleZ                 | ラジアン→[-30, 30]   |
| face.x, face.y             | ParamBodyAngleX / BodyAngleY| 顔位置→体の揺れ      |
| blendshapes[eyeBlinkLeft]  | ParamEyeLOpen               | 反転・スケール        |
| blendshapes[eyeBlinkRight] | ParamEyeROpen               | 反転・スケール        |
| blendshapes[jawOpen]       | ParamMouthOpenY             | スケール              |
| blendshapes[mouthSmileLeft]| ParamMouthForm              | 左右平均              |
| handLandmarks              | ParamArmL / ParamArmR       | 独自計算              |

- 各パラメータにスムージング (ローパスフィルタ / Lerp) をかけてぎこちなさを除去

#### 3-3. オフスクリーンレンダリング
- Live2D を ID3D11Texture2D (RGBA) にオフスクリーン描画
- そのテクスチャをImGuiのプレビューパネルに表示
- 同じテクスチャを仮想カメラ出力にも流用


---

### Phase 4 — 仮想カメラ (2〜3週間)

#### 4-1. DirectShow フィルタ設計
- COM実装: IBaseFilter / IPin / IAMStreamConfig
- 別DLLとしてビルド: NohCamVirtualCamera.dll

#### 4-2. フレーム共有方式
```
NohCam本体 (EXE) ↔ DirectShowフィルタ (DLL) 間のフレーム受け渡し
EXE側 (FrameExporter)
DX11 RenderTarget → GetData → BGRA byte[] → 名前付き共有メモリ (CreateFileMapping)
→ セマフォで新フレームを通知
DLL側 (VirtualCamPin)
セマフォ待機 → 共有メモリ読み出し → IMemInputPin::Receive でZoomへ渡す
```
#### 4-3. DLL 登録
- インストーラー or 初回起動時に regsvr32 相当の処理を実行
- HKCR\CLSID\{NohCam-GUID} にフィルタ情報を登録
- Teams / Zoom 側がカメラ一覧に "NohCam Virtual Camera" と表示される


---

### Phase 5 — UI 仕上げ・設定 (1週間)

#### Dear ImGui パネル構成

```
┌─────────────────────────────────────────────┐
│  NohCam                          [最小化][✕] │
├───────────────┬─────────────────────────────┤
│               │  📷 Camera                  │
│               │  デバイス: [FaceTime HD ▼]  │
│  [アバター    │  解像度:   [1280x720   ▼]   │
│   プレビュー] ├─────────────────────────────┤
│               │  🎭 Avatar                  │
│               │  モデル: [Select .moc3...]  │
│               ├─────────────────────────────┤
│               │  🎯 Tracking                │
│               │  顔感度  ━━●━━━━  0.8      │
│               │  口スケール ━●━━━━  0.6    │
│               │  手トラッキング [ON]        │
│               ├─────────────────────────────┤
│               │  📡 Output                  │
│               │  仮想カメラ [● 配信中]      │
│               │  解像度: 1280x720 @ 30fps   │
└───────────────┴─────────────────────────────┘
```

#### 設定の永続化
- config.json に保存 (nlohmann/json 使用)
  - カメラデバイスID
  - モデルファイルパス
  - 各パラメータの感度・オフセット
  - 出力解像度・FPS


---

### Phase 6 — 品質・パフォーマンス (1週間)

#### パフォーマンス目標
| 処理               | 目標          |
|-------------------|---------------|
| トラッキング推論   | < 10ms / frame (GPU推論) |
| Live2D 描画        | < 5ms / frame |
| 仮想カメラ遅延     | < 100ms 総合  |
| CPU使用率          | < 20%         |

#### 最適化ポイント
- MediaPipe を GPU デリゲート (CUDA / DirectML) で動かす
- トラッキングは 30fps で十分、レンダリングは 60fps で別スレッド
- 共有メモリのゼロコピー化 (書き込みと読み出しでバッファを交互に使うダブルバッファリング)

#### エラーハンドリング
- カメラ切断時の自動再接続
- モデルファイル不正時のフォールバック表示
- DirectShowフィルタ未登録時のガイドダイアログ


---

## 開発順序まとめ

Phase 1 → ウィンドウ + DX11 + カメラ入力が動く
Phase 2 → 顔・手トラッキング結果がデバッグ表示される
Phase 3 → アバターが顔の動きに追従する  ← ここで最初の達成感
Phase 4 → Zoomで自分のアバターが映る    ← ここで実用になる
Phase 5 → UIが整って他人に使わせられる
Phase 6 → 軽くて安定する


---

## 主な依存ライブラリ バージョン一覧

| ライブラリ                  | バージョン  | 取得方法          |
|----------------------------|------------|-------------------|
| MediaPipe                  | 0.10.x     | GitHub ソースビルド |
| Cubism SDK for Native      | 5-r.1      | Live2D 公式サイト  |
| Dear ImGui                 | 1.90.x     | GitHub / vcpkg    |
| DirectX 11                 | OS同梱     | Windows SDK       |
| nlohmann/json              | 3.11.x     | vcpkg             |
| spdlog                     | 1.13.x     | vcpkg             |
| glm                        | 1.0.x      | vcpkg (行列演算用) |
