# LZRC

[日本語ドキュメントはこちら](README_ja.md)

LZRC is a reference implementation of an experimental general-purpose
compressor combining LZSS with a context-adaptive binary range coder. It has
five profiles, ranging from a 256-byte-window, 16-bit core intended to remain
portable to Z80-class systems through a 16 MiB-window, 32-bit profile.

The exact format and algorithm are described in
[`LZRC_implementation_spec.md`](LZRC_implementation_spec.md).

## Build

Initialize the GoogleTest submodule after cloning:

```console
git submodule update --init --recursive
```

Visual Studio 2026:

```console
cmake --preset vs2026-x64
cmake --build --preset vs2026-x64-debug
ctest --preset vs2026-x64-debug
```

Linux with Ninja and GCC or Clang:

```console
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

## CLI

Compress with profile 0 through 4:

```console
lzrc c0 input.bin output.lzrc
lzrc c4 input.bin output.lzrc
```

Decompression reads the profile and original size from the container header:

```console
lzrc d output.lzrc restored.bin
```

## Library API

The public C99 API is declared in `include/lzrc/lzrc.h`. Library streams omit
the five-byte CLI container header, so callers pass the profile and expected
decoded size explicitly.

Call `lzrc_compress_bound()` to size a compression buffer, then use
`lzrc_compress()` and `lzrc_decompress()`. All functions return an
`lzrc_result`; `lzrc_result_string()` provides a short diagnostic message.

## Development

Tests use GoogleTest and are registered with CTest. Development follows a
test-first workflow. Significant implementation and verification work is
recorded in [`WORKLOG.md`](WORKLOG.md).
