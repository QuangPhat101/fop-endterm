# Cách chạy chương trình
## Bước 1: **Tạo thư mục `data`** và chuẩn bị dữ liệu

```cmd
mkdir data
```

## Bước 2: **Chuẩn bị dữ liệu**: Sao chép các file dữ liệu cần thiết vào thư mục `data/` (định dạng .csv).

## Bước 3: Chuẩn bị và biên dịch
## A. Windows
### Build (có thể bỏ qua)
```powershell
g++ -O2 -DNDEBUG -Wall -Wextra -flto=auto -ffunction-sections -fdata-sections "-Wl,--gc-sections" -s -std=c++23 src\*.cpp -o release\24120117.exe -lws2_32 -lpsapi
```

### Chạy
```powershell
.\release\24120117.exe
```

Sau đó mở port (Mặc định) như sau:

```text
http://localhost:24117
```

### Đổi port
Khi chạy chương trình có thể đổi port mong muốn như sau (ví dụ đổi thành port 3000):
```powershell
.\release\24120117.exe 3000
.\release\24120117.exe --port=3000
$env:HALO_PORT="3000"; .\release\24120117.exe
```

Sau đó mở port (Ví dụ port 3000):
```text
http://localhost:3000
```

## B. Linux
### Build (có thể bỏ qua)
Build bằng Makefile

```bash
make linux
```

Hoặc

```bash
mkdir -p release
g++ -O2 -DNDEBUG -Wall -Wextra -flto=auto -ffunction-sections -fdata-sections "-Wl,--gc-sections" -s -std=c++23 src\*.cpp -o release\24120117.exe -lws2_32 -lpsapi
```

### Chạy

```bash
./release/24120117.exe
```

Mở port

```text
http://localhost:24117
```

### Đổi port

Khi chạy chương trình có thể đổi port mong muốn như sau (ví dụ đổi thành port 3000):
```powershell
.\release\24120117.exe 3000
.\release\24120117.exe --port=3000
$env:HALO_PORT="3000"; .\release\24120117.exe
```

Sau đó mở:
```text
http://localhost:3000
```
