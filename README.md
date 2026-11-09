## Wymagania

- CMake 3.16 lub nowszy
- kompilator C17
- OpenSSL 3.x z biblioteką Crypto

## Budowanie i testy

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Struktura

- `include/c01/` — publiczne interfejsy C
- `src/crypto/` — backend kryptograficzny
- `tests/` — testy wektorów i zachowania granicznego
- `docs/architecture.md` — architektura, metryki i kolejne etapy

Transformacja używa tej samej operacji do szyfrowania i odszyfrowywania.
Klucz testowy i licznik są przekazywane w pamięci.
