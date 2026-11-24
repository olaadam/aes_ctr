# AES-128-CTR: CPU, MPI i OpenCL

Projekt szyfruje i odszyfrowuje pliki algorytmem AES-128-CTR. Zawiera wariant
serialny oparty na OpenSSL EVP, równoległy wariant OpenMP oraz opcjonalne backendy
MPI i OpenCL.

CTR zapewnia poufność, ale nie wykrywa modyfikacji danych. Dla każdego szyfrowania
tym samym kluczem trzeba użyć unikalnego licznika początkowego. Przykładowe klucze
i liczniki w poleceniach służą wyłącznie do testów.

## Wymagania

- CMake 3.16 lub nowszy
- kompilator C17 z obsługą OpenMP
- OpenSSL 3.x z biblioteką Crypto
- opcjonalnie: implementacja MPI dla C i `mpiexec`
- opcjonalnie: OpenCL SDK i urządzenie obsługujące OpenCL

W Windows użyj Developer PowerShell dla Visual Studio z workloadem **Desktop
development with C++**. Jeśli CMake nie znajdzie OpenSSL lub MPI, zobacz instrukcje
w `bench/CPU_MPI.md`.

## Budowanie CPU

```powershell
cmake -S . -B build -DC01_ENABLE_GPU=OFF -DC01_ENABLE_MPI=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Testy sprawdzają wektor NIST AES-CTR, niepełny blok, granicę licznika oraz
zgodność OpenMP z wariantem serialnym.

## Szyfrowanie pliku

Program przyjmuje plik wejściowy, plik wyjściowy, 16-bajtowy klucz AES-128,
16-bajtowy licznik w postaci hex, rozmiar bufora w bajtach i tryb `serial` lub
`omp`:

```powershell
build\Release\aesctr.exe input.bin encrypted.bin `
  --key 000102030405060708090a0b0c0d0e0f `
  --ctr 00000000000000000000000000000000 `
  --buf 1048576 --mode omp
```

Odszyfrowanie używa tej samej operacji z tym samym kluczem i licznikiem,
zamieniając pliki wejściowy i wyjściowy:

```powershell
build\Release\aesctr.exe encrypted.bin recovered.bin `
  --key 000102030405060708090a0b0c0d0e0f `
  --ctr 00000000000000000000000000000000 `
  --buf 1048576 --mode omp
```

## Pomiary CPU i MPI

`bench_cpu.exe` mierzy serialne i OpenMP szyfrowanie bufora w pamięci.
Skrypt `bench/bench_cpu.ps1` mierzy cały potok plikowy dla różnych plików,
rozmiarów bufora i liczb wątków. Szczegółowe polecenia są w
[`bench/CPU_MPI.md`](bench/CPU_MPI.md).

MPI jest opcjonalne i buduje osobny program `aesctr_mpi`. Rangi dzielą bloki
pliku, liczą liczniki od globalnego indeksu i zapisują dane na ich pierwotnych
offsetach. Do pomiarów skalowania służy `bench/bench_mpi.ps1`.

## OpenCL

OpenCL jest domyślnie wyłączone. Włącz je przez `-DC01_ENABLE_GPU=ON`.
Backend GPU ma osobny benchmark i testy; instrukcje oraz dotychczasowe wyniki są
w [`bench/GPU_OPENCL.md`](bench/GPU_OPENCL.md).

## Układ projektu

- `include/` — publiczne interfejsy
- `src/crypto/` — OpenSSL EVP i OpenMP
- `src/cli/` — program do plików dla CPU
- `src/mpi/` — program plikowy MPI
- `src/backends/` — jądro i backend OpenCL
- `tests/` — testy CPU i GPU
- `bench/` — programy, skrypty i instrukcje pomiarów
- `results/` — zapisane wyniki benchmarków; lokalne pliki testowe są ignorowane przez Git
