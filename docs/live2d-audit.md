# Live2D モデル調査メモ

このドキュメントは、新しい Live2D モデルを追加したときに
次の 2 つの issue を更新するための手順をまとめたものです。

- パラメーター優先順位一覧: [#8](https://github.com/zenjiro/NohCam/issues/8)
- 表情プリセット一覧: [#9](https://github.com/zenjiro/NohCam/issues/9)

## 1. パラメーター一覧の調査

対象は runtime の `*.model3.json` です。
`*.cmo3` は編集用なので対象外です。

### 取得する情報

- モデル名
- `model3.json` の有無
- `live2d-py` で読んだ runtime parameter の `min` / `max`
- `default` が取れるなら `default`

### 重要な考え方

- `GetParameterCount()` と `GetParameter()` で runtime の全パラメーターを列挙する
- `GetParameterMinimumValue()` / `GetParameterMaximumValue()` で範囲を取る
- `GetParameterDefaultValue()` が使える場合は、初期値も記録できる
- `UPPER_SNAKE_CASE` と `PascalCase` は同一視して集計する
- パラメーターの有無を優先し、範囲差は補助情報として扱う

### 追加・更新時の流れ

1. 新しいモデルの `*.model3.json` を探す
2. `live2d-py` でロードして、全 parameter の ID と範囲をダンプする
3. 既存の表に対して、モデル列を追加する
4. 既出 parameter の count と observed ranges を更新する
5. 少数派の範囲には `*` を付ける

## 2. 表情一覧の調査

対象は `model3.json` の `FileReferences.Expressions` と、
ディスク上にある `*.exp3.json` です。

### 取得する情報

- `model3.json` に登録されている表情名
- 未登録だが存在する `*.exp3.json`
- `live2d-py` で表情一覧を取得できるか
- `LoadExtraExpression()` で追加読込できるか

### 重要な考え方

- `GetExpressions()` は、`model3.json` に登録済みの表情一覧を返す
- `LoadExtraExpression()` で、未登録の `*.exp3.json` を追加読込できる
- `SetExpression()` は表情 ID を指定して適用する
- `exp_01` のような名前は、内容を意味するとは限らない
- `*.exp3.json` の中には `FadeInTime` / `FadeOutTime` / `Parameters` が入る

### 追加・更新時の流れ

1. 新しいモデルの `*.model3.json` を読む
2. `FileReferences.Expressions` を列挙する
3. そのモデル配下の `*.exp3.json` を洗い出す
4. `model3.json` 未登録のものは別枠で記録する
5. 必要なら `live2d-py` で `LoadExtraExpression()` と `SetExpression()` を試す

## 3. Python 例

### 登録済み表情の一覧を取る

```python
from live2d.v3 import Model, init

init()
model = Model()
model.LoadModelJson("assets/live2d-models/Epsilon_free/runtime/Epsilon_free.model3.json")

print(model.GetExpressions())
```

### 未登録の表情ファイルを追加して使う

```python
from live2d.v3 import Model, init

init()
model = Model()
model.LoadModelJson("assets/live2d-models/ASUKA/Asuka.model3.json")

model.LoadExtraExpression(
    "Gloom",
    "assets/live2d-models/ASUKA/EXPRESSIONS/Gloom.exp3.json",
)
model.SetExpression("Gloom")
```

### パラメーターの min / max を取る

```python
from live2d.v3 import Model, init

init()
model = Model()
model.LoadModelJson("assets/live2d-models/Epsilon_free/runtime/Epsilon_free.model3.json")

count = model.GetParameterCount()
for i in range(count):
    param = model.GetParameter(i)
    print(param.id, param.min, param.max, param.default)
```

## 4. 更新ルール

- 新しいモデルを追加したら、まず `#8` と `#9` のどちらに影響があるか確認する
- パラメーター優先順位は `#8`
- 表情プリセットは `#9`
- 迷ったら「実際に runtime で取れた情報」を優先する

