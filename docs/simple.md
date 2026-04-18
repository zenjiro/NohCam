# simple2 モデル構造

## ディレクトリ構成

```
simple2/
├── ReadMe.txt                 # 説明ファイル
├── simple_t01.can3            # キャンバス情報
├── simple_t01.cmo3           # コンパイル済みモデル
└── runtime/
    ├── motion/               # アクション定義
    │   └── Scene.motion3.json
    ├── simple.1024/          # テクスチャ（画像）
    │   └── texture_00.png
    ├── simple.cdi3.json      #  表示情報
    ├── simple.moc3          # コンパイル済みモデルデータ
    └── simple.model3.json    # モデル設定
```

## simple.model3.json の構造

| フィールド | 説明 |
|-----------|------|
| `Version` | Cubism バージョン (3) |
| `FileReferences.Moc` | モデルデータファイル |
| `FileReferences.Textures` | テクスチャ（画像）ファイルの配列 |
| `FileReferences.DisplayInfo` | 表示情報ファイル |
| `FileReferences.Motions` | アクション定義 |
| `Groups` | パラメータグループ（自動まばたき、口パクなど） |
| `HitAreas` | クリック判定エリア |

## 画像を変更する方法

`runtime/simple.1024/texture_00.png` を置き換えるだけでは**不足**です。

### 正しい手順

1. **画像ファイル名を一致させる**: `simple.model3.json` の `Textures` 配列に `"simple.1024/texture_00.png"` とあるので、`texture_00.png` という名前で `simple.1024/` ディレクトリに保存
2. **画像尺寸を確認**: 1024x1024 程度推奨
3. ** Live2D Cubism Editor で再エクスポート**するのが安全（画像だけ替えるとずれる）

### 注意点

- ファイル名は `simple.model3.json` の `Textures` 配列と一致させる
- `simple.1024/texture_00.png` は Live2D Editor で開く必要があり、画像だけ替えると 表示が崩れる可能性がある
- Live2D Cubism Editor でモデルを開いて画像を置き換え、エクスポートするのが確実