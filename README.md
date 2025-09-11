# system-monitor

C言語と Windows API (`windows.h`) だけで作成したシステムモニター風アプリです。  
CPU・メモリ使用率をリアルタイムの折れ線グラフで表示します。  
ダークモードやログ出力,ウィンドウサイズ変更に対応。

2025/09/11 クライアント/ホスト機能の実装

## ダウンロード
最新版は [Releases](https://github.com/mizuki20070314/system-monitor/releases) から実行ファイルを入手できます。  
解凍後に `system-monitor.exe` をダブルクリックしてください。 

## 機能
- CPU・メモリ使用率のリアルタイム折れ線グラフ
- グリッド表示
- ダークモード切替 (Dキー)
- 最新値をグラフ右端に強調表示
- CSVログ出力機能 (Lキー)
- ウィンドウサイズの変更で再描画
- サーバーを経由して、ホストで起動しているPCの状態をクライアントで確認(起動時に入力するユーザー名を同じにしてください)

## スクリーンショット
<img width="444" height="222" alt="image" src="https://github.com/user-attachments/assets/30825936-a63d-4bed-831f-ad687c4bf769" />
<img width="1478" height="1054" alt="image" src="https://github.com/user-attachments/assets/d601adf0-7100-4ad1-b5f7-d035642aef01" />

## ビルド方法
Windows 環境で以下のコマンドを実行：
```bash
gcc src/main.c -o bin/main.exe -mwindows -lwinhttp
