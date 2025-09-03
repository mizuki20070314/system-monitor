# system-monitor

C言語と Windows API (`windows.h`) だけで作成したシステムモニター風アプリです。  
CPU・メモリ使用率をリアルタイムの折れ線グラフで表示します。  
ダークモードやウィンドウサイズ変更に対応。

## 機能
- CPU・メモリ使用率のリアルタイム折れ線グラフ
- グリッド表示
- ダークモード切替（Dキー）
- 最新値をグラフ右端に強調表示
- CSVログ出力機能

## スクリーンショット
<img width="1178" height="739" alt="image" src="https://github.com/user-attachments/assets/7aa9029f-f8dc-4e1a-970a-24b299f9f07e" />


## ビルド方法
Windows 環境で以下のコマンドを実行：
```bash
gcc src/main.c -o bin/main.exe -mwindows
