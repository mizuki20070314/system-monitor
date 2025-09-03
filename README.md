# system-monitor

C言語と Windows API (`windows.h`) だけで作成したシステムモニター風アプリです。  
CPU・メモリ使用率をリアルタイムの折れ線グラフで表示します。  
ダークモードやウィンドウサイズ変更に対応。

## ダウンロード
最新版は [Releases](https://github.com/mizuki20070314/system-monitor/releases) から実行ファイルを入手できます。  
解凍後に `system-monitor.exe` をダブルクリックしてください。 

## 機能
- CPU・メモリ使用率のリアルタイム折れ線グラフ
- グリッド表示
- ダークモード切替（Dキー）
- 最新値をグラフ右端に強調表示
- CSVログ出力機能

## スクリーンショット
<img width="1178" height="739" alt="image" src="https://github.com/user-attachments/assets/adb10489-452b-4adb-8c30-f0bda6ecb5a3" />


## ビルド方法
Windows 環境で以下のコマンドを実行：
```bash
gcc src/main.c -o bin/main.exe -mwindows
