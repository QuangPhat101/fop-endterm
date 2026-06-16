# RULE.md - HALO Endterm

Tài liệu này mô tả đúng trạng thái hiện tại của project HALO: cách build/chạy,
quy ước code, luồng dữ liệu, API và các detector anomaly đã implement.

## 1. Mục Tiêu Project

- Nạp file CSV log trong thư mục `data/`.
- Chuẩn hóa và index dữ liệu bằng các cấu trúc tự viết.
- Hỗ trợ 3 query chính:
  - hành trình của user theo thời gian;
  - lịch sử truy cập của resource theo thời gian;
  - top resource được truy cập nhiều nhất.
- Hỗ trợ phát hiện bất thường trên toàn bộ dataset đã nạp.
- Có UI web local, backend HTTP native C++.
- Build được trên Windows và Linux.
- File nộp/chạy chính tên là `release/24120117.exe` trên cả Windows và Linux.

## 2. Yêu Cầu Môi Trường

### A. Windows

Cần có:

- Windows 10/11.
- MinGW-w64 GCC có `g++`.
- `mingw32-make` nếu build bằng Makefile.

Có thể cài MSYS2 bằng:

```powershell
winget install MSYS2.MSYS2
```

Sau đó mở MSYS2 UCRT64 và cài compiler:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make
```

Build:

```powershell
mingw32-make windows
```

Hoặc build trực tiếp:

```powershell
if not exist release mkdir release
g++ -O2 -DNDEBUG -ffunction-sections -fdata-sections "-Wl,--gc-sections" -s -std=c++23 src\*.cpp -o release\24120117.exe -lws2_32 -lpsapi
```

Chạy:

```powershell
.\release\24120117.exe
```

### B. Linux

Cần có:

- Linux 64-bit.
- `g++` hỗ trợ C++23.
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

Build:

```bash
make linux
```

Hoặc build trực tiếp:

```bash
mkdir -p release
g++ -O2 -DNDEBUG -ffunction-sections -fdata-sections -Wl,--gc-sections -s -std=c++23 src/*.cpp -o release/24120117.exe
```

Chạy:

```bash
./release/24120117.exe
```

## 3. Port Backend

Port mặc định của backend là `24117`.

Mở UI tại:

```text
http://localhost:24117
```

Có 3 cách đổi port:

Windows:

```powershell
.\release\24120117.exe 3000
.\release\24120117.exe --port=3000
$env:HALO_PORT="3000"; .\release\24120117.exe
```

Linux:

```bash
./release/24120117.exe 3000
./release/24120117.exe --port=3000
HALO_PORT=3000 ./release/24120117.exe
```

Nếu đổi port thành `3000`, mở:

```text
http://localhost:3000
```

## 4. Cấu Trúc File Quan Trọng

- `src/main.cpp`: backend HTTP, route API, load dataset, cache `.dat`, checkpoint import, chọn port.
- `index.html`: UI web local.
- `src/Halo.h`, `src/Halo.cpp`: engine chính, query, index, detector anomaly.
- `src/DataReader.h`, `src/DataReader.cpp`: đọc và parse CSV.
- `src/DataRecords.h`: schema record nội bộ.
- `src/RecordStorage.h`, `src/RecordStorage.cpp`: lưu record, sort, deduplicate.
- `src/Vector.h`: vector tự viết, thay thế `std::vector`.
- `src/HashTable.h`, `src/HashTable.cpp`: hash table tự viết.
- `src/idTable.h`, `src/idTable.cpp`: map ID chuỗi sang ID số nội bộ.
- `src/Platform.h`, `src/Platform.cpp`: lớp cross-platform cho file, socket, memory.
- `src/Sort.h`: các sort generic tự viết.
- `tests/anomaly_tests.cpp`: test cho một số detector anomaly.
- `Makefile`: build Windows/Linux/test.
- `data/`: đặt file `.csv`, cache `.dat`, checkpoint và kết quả anomaly.
- `data/anomaly_results/`: nơi lưu file kết quả anomaly `.jsonl`.

## 5. Định Dạng Dữ Liệu Vào

File CSV đặt trong `data/`.

Mỗi dòng data cần có 7 trường:

```text
user_id,device_id,app_id,resource_id,event_type,location,timestamp
```

Quy ước ID:

- `user_id`: bắt đầu bằng `U`, ví dụ `U1`.
- `device_id`: bắt đầu bằng `D`, ví dụ `D1`.
- `app_id`: bắt đầu bằng `APP`, ví dụ `APP1`.
- `resource_id`: bắt đầu bằng `R`, ví dụ `R1` hoặc `R00001`.
- `timestamp`: số nguyên không âm, tính bằng epoch seconds.

Event type hỗ trợ:

- `LOGIN`
- `LOGOUT`
- `TOKEN_REFRESH`
- `ACCESS`
- `FAILED_LOGIN`
- `OPEN_APP`
- `DOWNLOAD`
- `ADMIN_ACTION`

Location hỗ trợ:

- `US`, `VN`, `FR`, `DE`, `CN`, `SG`, `KR`, `JP`, `UK`, `AU`, `CA`, `IN`, `BR`, `RU`, `TH`

Xử lý dòng lỗi:

- Sai số cột hoặc timestamp không parse được: bỏ qua dòng.
- ID sai format: thay bằng `UNKNOWN_USER`, `UNKNOWN_DEVICE`, `UNKNOWN_APP`, `UNKNOWN_RESOURCE`.
- Event/location sai: thay bằng enum unknown.
- Record trùng hoàn toàn sau khi load sẽ được gộp bằng field `Count`.

## 6. API Hiện Có

Backend trả JSON cho các route:

- `GET /`: trả UI HTML.
- `GET /api/list-datasets`: liệt kê file `.csv` trong `data/`.
- `GET /api/list-loaded`: liệt kê workspace đang load hoặc cache `.dat` hợp lệ.
- `GET /api/load-dataset?file=<csv>`: load dataset.
- `GET /api/stats?loadedFile=<csv>`: thống kê workspace.
- `GET /api/query-user?loadedFile=<csv>&userId=<id>&startTime=<t>&endTime=<t>`
- `GET /api/query-resource?loadedFile=<csv>&resourceId=<id>&startTime=<t>&endTime=<t>`
- `GET /api/query-top?loadedFile=<csv>&startTime=<t>&endTime=<t>`
- `GET /api/anomalies?loadedFile=<csv>&type=<TYPE>&...params`
- `GET /api/list-anomaly-files`: liệt kê file kết quả `.jsonl`.
- `GET /api/get-anomaly-file-content?file=<filename>`: đọc preview file anomaly.

Lưu ý hiện tại:

- UI có nút xóa lịch sử anomaly, nhưng backend chưa có route
  `/api/delete-anomaly-file` và `/api/clear-anomaly-files`.
- Các route nhận `file`/`loadedFile` cần được validate nếu mở server ra mạng ngoài.

## 7. Detector Anomaly Đã Implement

Detector đang chạy trong `Halo::detectAnomalies`.

### Nhóm A

- `A1`: user login từ quá nhiều device khác nhau trong cửa sổ thời gian.
  - Params: `n`, `windowSec`.
- `A2`: chuỗi `FAILED_LOGIN` liên tiếp của cùng user.
  - Params: `n`.
- `A3`: device truy cập quá nhiều resource khác nhau trong cửa sổ thời gian.
  - Params: `n`, `windowSec`.
- `A4`: hoạt động ngoài giờ làm việc theo timezone location.
  - Params: `startHour`, `endHour`.
- `A5`: impossible travel giữa 2 quốc gia không chung biên giới.
  - Dùng tọa độ đại diện, Haversine, tốc độ bay 900 km/h, cộng 2 giờ sân bay.
- `A6`: user đổi location quá nhiều lần trong cùng ngày UTC.
  - Params: `n`.
- `A7`: phiên `LOGIN -> LOGOUT` quá dài.
  - Params: `sessionSec`.
- `A8`: user tạo quá nhiều login trong cửa sổ thời gian.
  - Params: `n`, `windowSec`.
- `A9`: trong phiên có `ADMIN_ACTION` rồi nhiều `DOWNLOAD`.
  - Params: `n`.

### Nhóm B

- `B1`: brute force thành công: nhiều `FAILED_LOGIN`, sau đó `LOGIN`.
  - Params: `n`.
- `B2`: im lặng lâu rồi burst hoạt động.
  - Params: `silenceSec`, `burstSec`, `n`.

### Nhóm C

- `C4`: low-and-slow brute force, các `FAILED_LOGIN` rải rác cách nhau ít nhất `minSpacingSec`.
  - Params: `n`, `minSpacingSec`, `windowSec`.

### Nhóm D

- `D2`: speed anomaly, nhiều khoảng cách event liên tiếp nhỏ hơn `minGapSec`.
  - Params: `minGapSec`, `n`.
- `D8`: robot timing, hoạt động đều bất thường theo CV trong ngày.
  - Params: `minEvents`, `maxCvPercent`.
- `D10`: một location có tỉ lệ `FAILED_LOGIN` cao bất thường trong cửa sổ thời gian.
  - Params: `windowSec`, `minEvents`, `failureRatioPercent`.
- `D11`: resource scan tuần tự theo mã `R...` tăng liên tiếp.
  - Params: `coveragePercent`.

### Nhóm E

- `E1`: nhiều phiên truy cập siêu ngắn có truy cập dữ liệu.
  - Params: `maxDurationSec`, `n`.

## 8. Output Anomaly

Khi gọi `/api/anomalies`, output có 2 chế độ:

- `output=screen`: trả preview JSON về UI.
- `output=file`: ghi toàn bộ kết quả vào file `.jsonl`.

File kết quả nằm trong:

```text
data/anomaly_results/
```

Tên file:

```text
anomaly_<TYPE>_<timestamp>.jsonl
```

UI hiển thị preview tối đa 500 dòng khi đọc lại file lịch sử.

## 9. Quy Tắc Code Bắt Buộc

- Mọi `if`, `for`, `while` phải có block `{}`.
- Biến nên được khởi tạo lúc khai báo.
- Mỗi dòng chỉ khai báo một biến cùng một kiểu dữ liệu.
- Không dùng `std::vector`, `std::map`, `std::unordered_map`, `std::set`.
- Không dùng thư viện đồ thị có sẵn.
- Được dùng `std::string` và file I/O.
- Nếu cấp phát động thì phải có cleanup rõ ràng.
- Ưu tiên RAII/destructor cho class tự quản lý tài nguyên.
- Không đưa logic mới vào sai module:
  - cấu trúc mảng động: `Vector.h`;
  - hash lookup: `HashTable.*`, `idTable.*`;
  - sort generic: `Sort.h`;
  - lưu/sort/dedup record: `RecordStorage.*`;
  - parse CSV: `DataReader.*`;
  - platform/socket/file/memory: `Platform.*`;
  - query và anomaly: `Halo.*`;
  - route HTTP và orchestration: `main.cpp`.

## 10. Test

Windows:

```powershell
mingw32-make test-windows
```

Hoặc:

```powershell
g++ -O2 -Wall -Wextra -std=c++23 -Isrc tests\anomaly_tests.cpp src\Halo.cpp src\RecordStorage.cpp src\idTable.cpp src\HashTable.cpp src\Platform.cpp -o release\anomaly_tests.exe -lws2_32 -lpsapi
.\release\anomaly_tests.exe
```

Linux:

```bash
make test-linux
```

Hoặc:

```bash
mkdir -p release
g++ -O2 -Wall -Wextra -std=c++23 -Isrc tests/anomaly_tests.cpp src/Halo.cpp src/RecordStorage.cpp src/idTable.cpp src/HashTable.cpp src/Platform.cpp -o release/anomaly_tests_linux
./release/anomaly_tests_linux
```
