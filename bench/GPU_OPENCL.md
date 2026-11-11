# Część GPU (OpenCL): stan prac

## Pliki

- `src/backends/aes_ctr.cl`: jądro AES-128-CTR (OpenCL C).
- `include/c01/gpu.h`, `src/backends/gpu_opencl.c`: backend GPU w C.
- `tests/test_gpu.c`: testy T1-T7, porównanie z wrapperem `c01_aes128_ctr_transform`.
- `bench/bench_gpu.c`: pomiar wpływu rozmiaru partii.

## Budowanie i uruchomienie

Wymaga `vcpkg install openssl:x64-windows opencl:x64-windows`.

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
ctest --test-dir build -C Release
build/Release/bench_gpu.exe 256 5
```

Bez OpenCL: `-DC01_ENABLE_GPU=OFF`.

## Co działa

- Jądro: jeden work-item = jeden blok 16 B, licznik `ctr0 + block_offset + get_global_id(0)` (64-bit, z przeniesieniem do starszego słowa), T-tablica i S-box w pamięci `__local`, ostatni niepełny blok.
- Wersja synchroniczna: H2D, jądro, D2H dla każdej partii; czasy etapów ze zdarzeń OpenCL z profilowaniem.
- Kontrola reguły R1 (zakres licznika nie może przekroczyć 2^128) przed startem.
- Testy T1-T7 przechodzą.

## Pierwsze wyniki (2026-11-11, 256 MiB w pamięci, mediana z 5 przebiegów)

Środowisko: Intel Core i7-1185G7, 15,7 GB RAM, Intel Iris Xe (sterownik 32.0.101.7088, OpenCL 3.0 NEO),
Windows 11, MSVC 2022, OpenSSL 3.6.5 (vcpkg).

| Partia | Jądro GB/s | Z transferami GB/s | H2D ms | Jądro ms | D2H ms |
|---|---|---|---|---|---|
| 1 MiB | 11.15 | 5.36 | 14.6 | 24.1 | 11.6 |
| 4 MiB | 10.84 | 3.09 | 17.7 | 24.8 | 44.4 |
| 16 MiB | 7.95 | 2.69 | 22.7 | 33.7 | 43.6 |
| 64 MiB | 7.46 | 2.53 | 28.8 | 36.0 | 41.2 |
| 256 MiB | 6.47 | 2.14 | 34.3 | 41.5 | 49.6 |

OpenSSL (1 wątek CPU, AES-NI): 5.96 GB/s.

Wniosek wstępny: samo jądro jest szybsze od OpenSSL, ale po doliczeniu kopiowania w obie strony GPU
przegrywa. Małe partie (1 MiB) mieszczą się w pamięci podręcznej CPU, więc kopiowanie jest tańsze
i GPU z transferami prawie dorównuje OpenSSL. Na Iris Xe „transfer” to kopiowanie w tej samej
pamięci RAM, więc następny krok to wariant zero-copy.

## Następne kroki

1. Wariant zero-copy (`CL_MEM_USE_HOST_PTR`, `clEnqueueMapBuffer`).
2. Podwójne buforowanie: dwie kolejki poleceń, potok plikowy.
3. Podłączenie `--mode gpu` do wspólnego programu `aesctr`, gdy powstanie CLI.
