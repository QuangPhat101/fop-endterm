1. Câu lệnh: g++ -O2 -DNDEBUG -Wall -Wextra -flto=auto -ffunction-sections -fdata-sections "-Wl,--gc-sections" -s -std=c++23 src\*.cpp -o release\24120117.exe -lws2_32 -lpsapi
2. ### Cần có

- Windows 10/11.
- MinGW-w64 GCC co `g++`.
- Neu muon build bang Makefile: can `mingw32-make`.

Neu dung MSYS2, co the cai compiler va make bang:

```powershell
winget install MSYS2.MSYS2
```

Sau do mo **MSYS2 UCRT64** va chay:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make
```

Dam bao `g++` va `mingw32-make` nam trong `PATH`. Kiem tra:

```powershell
g++ --version
mingw32-make --version
```

3.

### Can co

- Linux 64-bit.
- `g++` ho tro C++23.
- `make`.

Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y build-essential
```

Fedora:

```bash
sudo dnf install -y gcc-c++ make
```

Arch Linux:

```bash
sudo pacman -S --needed base-devel
```

Kiem tra:

```bash
g++ --version
make --version
```
