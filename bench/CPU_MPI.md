# CPU i MPI: uruchamianie pomiarów

## Budowanie

Wariant CPU:

```powershell
cmake -S . -B build -DC01_ENABLE_GPU=OFF
cmake --build build --config Release
```

Wariant MPI wymaga implementacji MPI dostępnej dla C i `mpiexec`:

```powershell
cmake -S . -B build-mpi -DC01_ENABLE_GPU=OFF -DC01_ENABLE_MPI=ON
cmake --build build-mpi --config Release
```

## Pomiar samego szyfrowania CPU

`bench_cpu` mierzy wariant serialny i OpenMP na buforze w pamięci, bez dostępu
do pliku. Pierwsze wywołanie każdego wariantu jest rozgrzewkowe.

```powershell
build\Release\bench_cpu.exe 67108864 7 results\cpu_kernel.csv
```

## Potok plikowy i buforowanie OpenMP

`bench_cpu.ps1` mierzy cały czas uruchomienia CLI, obejmujący odczyt, szyfrowanie
i zapis. Można podać kilka plików wejściowych, rozmiarów bufora i liczb wątków;
wyniki można zapisać do CSV. Bufory muszą mieć rozmiar będący wielokrotnością
16 bajtów. Odczyt, obliczenia i zapis są synchroniczne; skrypt pozwala zmierzyć
wpływ wielkości porcji, pliku i liczby wątków.

```powershell
.\bench\bench_cpu.ps1 `
  -Aesctr .\build\Release\aesctr.exe `
  -InputFile .\data\small.bin,.\data\large.bin `
  -Key 000102030405060708090a0b0c0d0e0f `
  -Counter 00000000000000000000000000000000 `
  -BufferBytes 65536,1048576,4194304 `
  -ThreadCounts 1,2,4,8 -Runs 5 -OutputCsv .\results\cpu_pipeline.csv
```

## MPI: podział pliku i skalowanie

`aesctr_mpi` dzieli globalne bloki 16-bajtowe między rangi. Każda ranga
wylicza swój licznik z licznika początkowego i globalnego numeru bloku, czyta
porcje przez `MPI_File_read_at` i zapisuje je pod ich pierwotnymi offsetami
przez `MPI_File_write_at`. Ostatni niepełny blok pozostaje na ostatniej randze.
Zakres licznika całego pliku jest sprawdzany przed rozpoczęciem zapisu.

```powershell
mpiexec -n 4 .\build-mpi\Release\aesctr_mpi.exe `
  .\data\input.bin .\results\output.bin `
  --key 000102030405060708090a0b0c0d0e0f `
  --ctr 00000000000000000000000000000000 `
  --buf 1048576 --mode serial
```

Raport MPI oddziela czas rozesłania konfiguracji, odczyt danych przez MPI-IO,
obliczenia, zapis oraz całkowity czas potoku. `bench_mpi.ps1` wykonuje serie
dla różnych liczb rang i rozmiarów bufora, a wyniki może zapisać do CSV:

```powershell
.\bench\bench_mpi.ps1 `
  -Mpiexec 'C:\Program Files\Microsoft MPI\Bin\mpiexec.exe' `
  -AesctrMpi .\build-mpi\Release\aesctr_mpi.exe `
  -InputFile .\data\large.bin `
  -Key 000102030405060708090a0b0c0d0e0f `
  -Counter 00000000000000000000000000000000 `
  -Ranks 1,2,4,8 -BufferBytes 65536,1048576,4194304 `
  -Mode serial -Runs 3 -OutputCsv .\results\mpi_scaling.csv
```

Tryb CTR nie zapobiega ponownemu użyciu tego samego klucza i licznika między
oddzielnymi uruchomieniami. Należy zapewnić unikalny licznik dla
każdego szyfrowania tym samym kluczem.
