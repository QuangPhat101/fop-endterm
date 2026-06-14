# HALO Endterm

## Data

Dat cac file `.csv` vao `data/`.

```text
data/
  halo_dataset_1m.csv
  halo_dataset_1_5m.csv
```

## Windows

```powershell
g++ -O2 -DNDEBUG -Wall -std=c++23 src\*.cpp -o release\24120117.exe -lws2_32 -lpsapi
.\release\24120117.exe
```

Mo trinh duyet tai:

```text
http://localhost:24117
```

Chon file trong `data/`, bam `Load dataset`, roi dung 3 query hoac phan phat hien bat thuong.

## Linux

Backend native Linux chua hoan chinh. Ke hoach port nam o `POSIX_PORT_PLAN.md`.

Sau khi port xong:

```bash
make linux
./release/halo
```

## Test

```powershell
g++ -O2 -DNDEBUG -Wall -std=c++23 -Isrc tests\parser_tests.cpp -o release\parser_tests.exe
.\release\parser_tests.exe

powershell -ExecutionPolicy Bypass -File tests\api_smoke.ps1
```
