# PX4Signal

BonDriver_PX4 の誤り率 (BER・PER) を、TVTest のステータスバーに C/N と並べて表示するプラグインです。

BonDriver の `GetSignalLevel()` は値を 1 つしか返せないため、C/N の表示を残したまま誤り率を見ることができません。
このプラグインは、BonDriver_PX4.dll が公開する `PX4GetSignalStats` 関数から値を取得して表示します。
BonDriver の `SignalLevel` 設定は既定の `CNR` のままで使えます。

## 必要なもの

- `PX4GetSignalStats` 関数を公開している BonDriver_PX4
- TVTest (32bit または 64bit)

誤り率を取得できる機種は、現在 PX-MLT5PE、PX-MLT8PE などの PXMLT 系の地上デジタルと BS/CS に限られます。
ほかの機種では `--` と表示されます。

## 使い方

1. TVTest のビット数に合わせて、32bit 版または 64bit 版の `PX4Signal.tvtp` を TVTest の `Plugins` フォルダーへコピーします。
2. TVTest の設定の「ステータスバー」で「BER/PER (PX4)」を表示します。

ステータスバーには `B BER 1.2E-05 PER 0` のように、階層、RS 復号前の BER、RS 復号後のパケット誤り率が表示されます。
既定では地上デジタルは B 階層、BS/CS は高階層を表示し、項目をクリックすると自動、A、B、C の順に切り替わります。
BS/CS では A が「高」、B が「低」と表示されます。
選択した階層は、プラグインと同じフォルダーの `PX4Signal.ini` に保存されます。

BER の測定期間は 512 パケット (約 84 万ビット) のため、`0` は BER がおよそ 1.2e-6 未満であることを示します。
復号できないほど信号が弱い場合、BER は 4.0E-02 付近で頭打ちになり、PER は 1.0E+00 (すべてのパケットが誤り) になります。
値を取得できない機種、選局前、測定期間の満了前は `--` が表示されます。

地上デジタルでは、先頭にビタビ復号前の BER が `VBER 4.0E-02  B BER 0 PER 0` のように表示されます。
ビタビ復号前の BER は電波の状態をそのまま表し、誤り訂正で完全に直っている間も 0 になりにくい値です。
地上デジタルでの換算の単位は資料で確認できていないため、同じ受信環境での比較に使う参考値として扱ってください。

## ビルド

Visual Studio 2022 (v143 toolset) で `PX4Signal.vcxproj` を開くか、MSBuild でビルドします。

```powershell
msbuild PX4Signal.vcxproj /p:Configuration=Release-static /p:Platform=Win32
msbuild PX4Signal.vcxproj /p:Configuration=Release-static /p:Platform=x64
```

`build\x86\Release-static\` と `build\x64\Release-static\` に `PX4Signal.tvtp` が作成されます。

## 同梱しているヘッダー

- `include/TVTestPlugin.h`: TVTest のプラグインヘッダーです。再配布・改変は自由とされています。
- `include/px4_signal_stats.h`: BonDriver_PX4 の `PX4GetSignalStats` 関数と構造体の定義です。BonDriver_PX4 側の定義と同じ内容に保ってください。

## ライセンス

[MIT License](LICENSE) です。
ただし `include/TVTestPlugin.h` は TVTest のプラグインヘッダーで、ファイル冒頭に記載された条件 (再配布・改変は自由) に従います。
