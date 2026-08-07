# LZRC

[English](README.md)

LZRCは、LZSSとコンテキスト適応型バイナリレンジコーダを組み合わせた、実験的な汎用圧縮アルゴリズムの参照実装です。  
本実装には5つのプロファイルが用意されており、Z80クラスのシステムへの移植性を維持した256バイトのウィンドウと16ビット処理を用いる構成から、16 MiBのウィンドウと32ビット処理を用いる構成までを網羅しています。

正確なフォーマットとアルゴリズムは、[`LZRC_implementation_spec.md`](LZRC_implementation_spec.md) に記載されています。

## ビルド

クローン後に GoogleTest サブモジュールを初期化します。

```console
git submodule update --init --recursive
```

Windows で Visual Studio 2026 を使用する場合。

```console
cmake --preset vs2026-x64
cmake --build --preset vs2026-x64-debug
ctest --preset vs2026-x64-debug
```

Linux で Ninja と GCC もしくは Clang でビルドする場合。

```console
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

## CLI

プロファイル0から4で圧縮するには以下のようにします。

```console
lzrc c0 input.bin output.lzrc
lzrc c4 input.bin output.lzrc
```

展開処理では、コンテナヘッダーからプロファイルと元のサイズを読み取ります。

```console
lzrc d output.lzrc restored.bin
```

## ライブラリ API

公開されている C99 API は `include/lzrc/lzrc.h` で宣言されています。ライブラリのストリームは 5 バイトの CLI コンテナ ヘッダーを省略するため、呼び出し元はプロファイルとデコード後の想定サイズを明示的に渡す必要があります。

必要な圧縮バッファのサイズを求めるには `lzrc_compress_bound()` を呼び出し、その後 `lzrc_compress()` と `lzrc_decompress()` を使用してください。すべての関数は `lzrc_result` を返します。`lzrc_result_string()` は短い診断メッセージを提供します。

## 開発

テストにはGoogleTestを使用し、CTestに登録しています。開発はテストファーストのワークフローに従って進められます。主要な実装や検証の作業内容は [`WORKLOG.md`](WORKLOG.md) に記録されています。
