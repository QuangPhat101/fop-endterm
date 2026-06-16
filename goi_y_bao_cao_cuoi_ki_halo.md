# Gợi ý nội dung báo cáo cuối kì HALO – Cyber Access Engine

> Mục tiêu của file này là làm khung nội dung cho báo cáo cuối kì. Báo cáo nên viết theo hướng **nâng cấp từ giữa kì**, không viết lại dài dòng các cấu trúc dữ liệu cũ như `Vector`, `HashTable`, `IdTable`, `RecordStorage`, cache `.dat`, checkpoint, HTTP server. Các phần đó chỉ nên nhắc ngắn là nền tảng đã có ở giữa kì, sau đó tập trung vào phần mới: **dữ liệu lớn, phát hiện bất thường, tham số truy vấn, kết quả thực nghiệm, cách chạy và đề xuất mở rộng**.

---

## 1. Định hướng chung cho báo cáo cuối kì

Báo cáo cuối kì nên có tinh thần như sau:

- Đây không phải là báo cáo làm lại từ đầu, mà là báo cáo **phiên bản nâng cấp** của HALO.
- Các cấu trúc dữ liệu chính đã trình bày ở giữa kì chỉ nên gom vào một mục ngắn: “Nền tảng kế thừa từ giữa kì”.
- Phần trọng tâm phải là:
  - khả năng xử lý dataset lớn tối thiểu 1.000.000 dòng;
  - module phát hiện bất thường;
  - các loại bất thường theo đề bài;
  - các bất thường tự đề xuất thêm;
  - cách cấu hình tham số;
  - cách chạy chương trình;
  - đánh giá hiệu năng và bộ nhớ;
  - hướng phát triển.

Có thể đặt tên báo cáo là:

> **BÁO CÁO ĐỒ ÁN CUỐI KÌ – HALO CYBER ACCESS ENGINE: LARGE-SCALE ANOMALY DETECTION**

---

## 2. Cấu trúc báo cáo nên viết

### Chương 1. Tổng quan nâng cấp cuối kì

Không nên lặp lại toàn bộ phần đặt vấn đề ở giữa kì. Chỉ cần viết ngắn:

- Giữa kì đã xây dựng engine đọc, lưu trữ, sắp xếp, truy vấn log theo thời gian.
- Cuối kì nâng cấp engine thành hệ thống phát hiện hành vi truy cập bất thường trên dữ liệu lớn.
- Dữ liệu vẫn gồm 7 trường: `user_id`, `device_id`, `app_id`, `resource_id`, `event_type`, `location`, `timestamp`.
- Điểm mới là hệ thống không chỉ trả lời “ai truy cập cái gì”, mà còn trả lời “hành vi nào đáng nghi”.

#### Đoạn mẫu có thể đưa vào báo cáo

```text
Ở giai đoạn giữa kì, HALO đã được xây dựng như một engine lưu trữ và truy vấn log truy cập theo thời gian. Hệ thống có khả năng đọc dữ liệu CSV, chuẩn hóa dữ liệu lỗi, ánh xạ chuỗi định danh sang số nguyên, sắp xếp theo timestamp và hỗ trợ các truy vấn cơ bản như hành trình User, lịch sử Resource và Top 10 Resource.

Trong giai đoạn cuối kì, đồ án tập trung mở rộng HALO thành hệ thống phát hiện bất thường trên dữ liệu lớn. Thay vì chỉ hiển thị lại lịch sử truy cập, hệ thống phân tích chuỗi sự kiện để tìm các dấu hiệu nghi vấn như đăng nhập thất bại liên tục, truy cập từ nhiều thiết bị, thay đổi vị trí bất thường, phiên làm việc kéo dài, chuỗi hành động nguy hiểm hoặc người dùng im lặng lâu rồi đột ngột hoạt động mạnh.
```

---

### Chương 2. Những phần kế thừa từ giữa kì

Mục này chỉ nên dài khoảng 1–2 trang. Không viết lại chi tiết từng cấu trúc dữ liệu.

Nên ghi bảng như sau:

| Thành phần kế thừa | Vai trò trong cuối kì | Có viết lại chi tiết không? |
|---|---|---|
| `DataRecords` | Là bản ghi nền cho mọi luật bất thường | Không, chỉ nhắc thêm nếu có field mới |
| `Vector<T>` | Lưu mảng động bản ghi và kết quả bất thường | Không |
| `HashTable`, `IdTable` | Ánh xạ User/Device/App/Resource sang ID số nguyên | Không |
| `RecordStorage` | Lưu, sort, binary search theo timestamp | Không |
| `DataReader` | Đọc CSV lớn, xử lý dòng lỗi, UNKNOWN | Không |
| Cache `.dat` | Nạp lại nhanh dataset đã xử lý | Không |
| Checkpoint | Giảm rủi ro khi import file lớn | Không |
| HTTP Server + Web UI | Nền tảng giao tiếp với người dùng | Chỉ ghi endpoint mới |

#### Đoạn mẫu

```text
Các cấu trúc dữ liệu nền như Vector tự cài đặt, HashTable, IdTable, RecordStorage và DataReader đã được trình bày trong báo cáo giữa kì. Ở báo cáo cuối kì, các thành phần này được xem là nền tảng có sẵn. Phần phát triển mới tập trung vào việc xây dựng các module phân tích bất thường dựa trên dữ liệu đã được sắp xếp theo thời gian và đã được chuẩn hóa.
```

---

### Chương 3. Các thành phần mới so với giữa kì

Đây là phần nên viết kỹ nhất.

#### 3.1. Module `AnomalyDetector`

Vai trò:

- Nhận dữ liệu đã sort theo timestamp từ `RecordStorage`.
- Nhận tham số cấu hình từ UI/API.
- Chạy từng luật phát hiện bất thường.
- Trả về danh sách `AnomalyResult`.

Gợi ý struct:

```cpp
struct AnomalyResult {
    int userID;
    int deviceID;
    int appID;
    int resourceID;
    event_Type eventType;
    location locationTag;
    uint64_t startTime;
    uint64_t endTime;
    int score;
    int count;
    string ruleName;
    string reason;
};
```

Ý nghĩa:

- `ruleName`: tên luật, ví dụ `FAILED_LOGIN_BURST`.
- `reason`: giải thích ngắn để người dùng hiểu vì sao bị đánh dấu.
- `score`: điểm nghi vấn, càng cao càng đáng chú ý.
- `count`: số lần vi phạm/ngưỡng bị vượt.

---

#### 3.2. Module `SessionBuilder`

Module này dùng để gom các sự kiện thành phiên làm việc.

Gợi ý định nghĩa phiên:

- Một phiên bắt đầu khi gặp `LOGIN` hoặc `OPEN_APP`.
- Một phiên kết thúc khi gặp `LOGOUT`.
- Nếu không có `LOGOUT`, phiên kết thúc tạm tại sự kiện cuối cùng của user/device/app đó.
- Nếu khoảng cách giữa hai sự kiện quá lớn, ví dụ lớn hơn `idleTimeout`, tự tách thành phiên mới.

Gợi ý struct:

```cpp
struct SessionInfo {
    int userID;
    int deviceID;
    int appID;
    uint64_t startTime;
    uint64_t endTime;
    int eventCount;
    int adminActionCount;
    int downloadCount;
    bool hasLogin;
    bool hasLogout;
};
```

Các luật cần `SessionBuilder`:

- Phiên làm việc dài bất thường.
- Nhiều phiên liên tục bất thường.
- Chuỗi `ADMIN_ACTION` + `DOWNLOAD` đáng ngờ.
- Phiên không logout.
- Phiên có download nhiều sau khi admin action.

---

#### 3.3. Module cấu hình tham số `RuleConfig`

Báo cáo nên nhấn mạnh rằng hệ thống **không hard-code ngưỡng**, mà cho người dùng nhập tham số.

Gợi ý struct:

```cpp
struct RuleConfig {
    uint64_t windowSeconds;
    int maxDevicesPerUser;
    int maxFailedLogin;
    int maxResourcesPerDevice;
    int workStartHour;
    int workEndHour;
    int maxCountriesPerUser;
    int maxLocationSwitches;
    uint64_t maxSessionDuration;
    int maxSessionsPerUser;
    uint64_t dormantDays;
    int burstEventThreshold;
};
```

Trong báo cáo nên ghi rõ: cùng một engine có thể chạy nhiều bộ tham số khác nhau để phù hợp với môi trường khác nhau.

---

#### 3.4. Endpoint/API mới

Nếu frontend có gọi API, báo cáo nên thêm bảng endpoint mới. Tên endpoint có thể chỉnh lại đúng với code thật.

| Endpoint đề xuất | Chức năng |
|---|---|
| `/api/anomaly/all` | Chạy toàn bộ luật phát hiện bất thường |
| `/api/anomaly/user-devices` | User dùng quá nhiều device trong thời gian ngắn |
| `/api/anomaly/failed-login` | Login thất bại liên tục |
| `/api/anomaly/device-resources` | Device truy cập nhiều resource khác nhau |
| `/api/anomaly/off-hours` | Truy cập ngoài giờ làm việc |
| `/api/anomaly/location` | Bất thường vị trí địa lý |
| `/api/anomaly/session` | Bất thường phiên làm việc |
| `/api/anomaly/advanced` | Các luật nâng cao |
| `/api/anomaly/custom` | Các luật tự đề xuất thêm |
| `/api/anomaly/export` | Xuất kết quả bất thường ra CSV/JSON nếu có |

---

## 4. Các phát hiện bất thường theo yêu cầu của thầy

Nên viết mỗi bất thường theo cùng một format để báo cáo nhìn chuyên nghiệp:

- Mục tiêu phát hiện.
- Tham số đầu vào.
- Cách phát hiện.
- Kết quả trả về.
- Độ phức tạp.

---

### 4.1. User đăng nhập từ quá nhiều device trong thời gian ngắn

| Nội dung | Gợi ý ghi |
|---|---|
| Tên luật | `USER_MULTI_DEVICE_SHORT_TIME` |
| Mục tiêu | Phát hiện tài khoản có thể bị chia sẻ hoặc bị chiếm quyền |
| Tham số | `windowSeconds`, `maxDevices` |
| Cách làm | Trong mỗi cửa sổ thời gian, đếm số device khác nhau của từng user có sự kiện `LOGIN` hoặc `OPEN_APP` |
| Điều kiện nghi vấn | `distinctDeviceCount > maxDevices` |
| Output | user, danh sách device, khoảng thời gian, số device |

Gợi ý mặc định:

- `windowSeconds = 300` giây.
- `maxDevices = 3`.

---

### 4.2. User login thất bại liên tục

| Nội dung | Gợi ý ghi |
|---|---|
| Tên luật | `FAILED_LOGIN_BURST` |
| Mục tiêu | Phát hiện dò mật khẩu/brute-force đơn giản |
| Tham số | `windowSeconds`, `maxFailedLogin` |
| Cách làm | Đếm số `FAILED_LOGIN` liên tiếp hoặc trong cùng cửa sổ thời gian theo user |
| Điều kiện nghi vấn | Số lần thất bại >= ngưỡng |
| Output | user, device, số lần thất bại, thời điểm bắt đầu/kết thúc |

Gợi ý mặc định:

- `windowSeconds = 600` giây.
- `maxFailedLogin = 5`.

---

### 4.3. Device đột ngột truy cập quá nhiều resource khác nhau

| Nội dung | Gợi ý ghi |
|---|---|
| Tên luật | `DEVICE_RESOURCE_SPIKE` |
| Mục tiêu | Phát hiện thiết bị có hành vi quét tài nguyên hoặc bị malware điều khiển |
| Tham số | `windowSeconds`, `maxDistinctResources` |
| Cách làm | Với mỗi device, đếm số resource khác nhau được `ACCESS` hoặc `DOWNLOAD` trong cửa sổ thời gian |
| Điều kiện nghi vấn | `distinctResourceCount > maxDistinctResources` |
| Output | device, user liên quan, số resource, danh sách resource mẫu |

Gợi ý mặc định:

- `windowSeconds = 600` giây.
- `maxDistinctResources = 20`.

---

### 4.4. Truy cập ngoài giờ làm việc

| Nội dung | Gợi ý ghi |
|---|---|
| Tên luật | `OFF_HOURS_ACCESS` |
| Mục tiêu | Phát hiện truy cập ngoài khung giờ bình thường |
| Tham số | `workStartHour`, `workEndHour`, `weekendMode` |
| Cách làm | Chuyển timestamp sang giờ trong ngày, đánh dấu các event `LOGIN`, `ACCESS`, `DOWNLOAD`, `ADMIN_ACTION` nằm ngoài khung giờ |
| Điều kiện nghi vấn | Event xảy ra trước giờ bắt đầu hoặc sau giờ kết thúc |
| Output | user, device, event, resource, timestamp, location |

Gợi ý mặc định:

- Giờ làm việc: 08:00–18:00.
- Có thể tăng điểm nghi vấn nếu event là `DOWNLOAD` hoặc `ADMIN_ACTION`.

---

### 4.5. User xuất hiện ở nhiều quốc gia không hợp lý

| Nội dung | Gợi ý ghi |
|---|---|
| Tên luật | `IMPOSSIBLE_COUNTRY_CHANGE` |
| Mục tiêu | Phát hiện tài khoản có dấu hiệu bị dùng từ nhiều nơi xa nhau |
| Tham số | `windowSeconds`, `maxCountries` |
| Cách làm | Trong một cửa sổ thời gian ngắn, đếm số location khác nhau của cùng user |
| Điều kiện nghi vấn | `distinctLocationCount > maxCountries` |
| Output | user, các location, thời gian, số lần đổi |

Gợi ý mặc định:

- `windowSeconds = 3600` giây.
- `maxCountries = 2`.

---

### 4.6. User liên tục đổi vị trí địa lý

| Nội dung | Gợi ý ghi |
|---|---|
| Tên luật | `LOCATION_SWITCHING` |
| Mục tiêu | Phát hiện user nhảy location liên tục trong log |
| Tham số | `windowSeconds`, `maxSwitches` |
| Cách làm | Duyệt sự kiện của từng user theo thời gian, mỗi lần location khác location trước thì tăng bộ đếm |
| Điều kiện nghi vấn | Số lần đổi location vượt ngưỡng |
| Output | user, chuỗi location, số lần đổi, khoảng thời gian |

Gợi ý mặc định:

- `windowSeconds = 3600` giây.
- `maxSwitches = 3`.

---

### 4.7. Phiên làm việc dài bất thường

| Nội dung | Gợi ý ghi |
|---|---|
| Tên luật | `LONG_SESSION` |
| Mục tiêu | Phát hiện phiên làm việc bất thường hoặc không logout |
| Tham số | `maxSessionDuration`, `idleTimeout` |
| Cách làm | Dùng `SessionBuilder` gom event từ `LOGIN` đến `LOGOUT` hoặc đến khi idle quá lâu |
| Điều kiện nghi vấn | Thời lượng phiên > ngưỡng |
| Output | user, device, app, startTime, endTime, duration |

Gợi ý mặc định:

- `maxSessionDuration = 8 giờ`.
- `idleTimeout = 30 phút`.

---

### 4.8. User tạo nhiều phiên liên tục bất thường

| Nội dung | Gợi ý ghi |
|---|---|
| Tên luật | `SESSION_BURST` |
| Mục tiêu | Phát hiện bot/script tự động login-logout liên tục |
| Tham số | `windowSeconds`, `maxSessions` |
| Cách làm | Sau khi dựng session, đếm số session của từng user trong cùng cửa sổ thời gian |
| Điều kiện nghi vấn | Số session vượt ngưỡng |
| Output | user, số session, khoảng thời gian |

Gợi ý mặc định:

- `windowSeconds = 3600` giây.
- `maxSessions = 10`.

---

### 4.9. Phiên có chuỗi hành động nguy hiểm: admin action và download liên tục

| Nội dung | Gợi ý ghi |
|---|---|
| Tên luật | `ADMIN_DOWNLOAD_CHAIN` |
| Mục tiêu | Phát hiện chuỗi hành động có khả năng rò rỉ dữ liệu hoặc lạm quyền |
| Tham số | `windowSeconds`, `minAdminActions`, `minDownloads` |
| Cách làm | Trong cùng session hoặc cửa sổ thời gian, kiểm tra `ADMIN_ACTION` xuất hiện gần các event `DOWNLOAD` |
| Điều kiện nghi vấn | Có admin action và số download vượt ngưỡng |
| Output | user, device, app, số admin action, số download, resource liên quan |

Gợi ý mặc định:

- `windowSeconds = 900` giây.
- `minAdminActions = 1`.
- `minDownloads = 5`.

---

### 4.10. Cố đăng nhập: thất bại nhiều lần rồi thành công

| Nội dung | Gợi ý ghi |
|---|---|
| Tên luật | `FAILED_THEN_SUCCESS_LOGIN` |
| Mục tiêu | Phát hiện tài khoản có thể đã bị đoán đúng mật khẩu sau nhiều lần sai |
| Tham số | `windowSeconds`, `minFailedBeforeSuccess` |
| Cách làm | Tìm chuỗi nhiều `FAILED_LOGIN` theo sau bởi `LOGIN` của cùng user/device hoặc cùng user/location |
| Điều kiện nghi vấn | Số thất bại trước lần thành công vượt ngưỡng |
| Output | user, device, số lần sai, thời điểm login thành công |

Gợi ý mặc định:

- `windowSeconds = 900` giây.
- `minFailedBeforeSuccess = 4`.

---

### 4.11. User im lặng rất lâu rồi đột ngột hoạt động mạnh

| Nội dung | Gợi ý ghi |
|---|---|
| Tên luật | `DORMANT_THEN_BURST` |
| Mục tiêu | Phát hiện tài khoản lâu không dùng bỗng hoạt động mạnh, có thể bị takeover |
| Tham số | `dormantDays`, `burstWindowSeconds`, `burstEventThreshold` |
| Cách làm | Với mỗi user, tìm khoảng cách lớn giữa hai event liên tiếp, sau đó đếm số event trong giai đoạn ngay sau khi user quay lại |
| Điều kiện nghi vấn | Im lặng lâu hơn `dormantDays` và burst event vượt ngưỡng |
| Output | user, thời gian im lặng, số event sau khi quay lại |

Gợi ý mặc định:

- `dormantDays = 7`.
- `burstWindowSeconds = 3600`.
- `burstEventThreshold = 20`.

---

## 5. Các phát hiện bất thường tự đề xuất thêm

Nên chọn khoảng 8–12 luật thêm mới. Không cần viết quá hàn lâm. Ưu tiên các luật nghe thực tế, dễ hiểu, có thể nhập tham số và truy vấn ra kết quả.

### Bảng đề xuất nhanh

| STT | Tên bất thường | Ý tưởng | Tham số nhập | Kết quả trả về |
|---:|---|---|---|---|
| 1 | `NEW_DEVICE_FOR_USER` | User dùng một device chưa từng xuất hiện trước đó | `lookbackDays`, `riskBoostIfOffHour` | user, device mới, lần đầu xuất hiện |
| 2 | `NEW_APP_FOR_USER` | User mở app chưa từng dùng trước đó, đặc biệt nếu sau đó download/admin | `lookbackDays`, `windowSeconds` | user, app mới, event sau đó |
| 3 | `SHARED_DEVICE_BY_MANY_USERS` | Một device được nhiều user khác nhau dùng trong thời gian ngắn | `windowSeconds`, `maxUsersPerDevice` | device, danh sách user, số user |
| 4 | `RESOURCE_SUDDEN_HOTSPOT` | Một resource đột nhiên bị nhiều user/device truy cập | `windowSeconds`, `maxAccess`, `maxUsers` | resource, số access, số user |
| 5 | `DOWNLOAD_BURST` | User/device download nhiều resource liên tục | `windowSeconds`, `maxDownloads` | user, device, số download, resource mẫu |
| 6 | `ADMIN_ACTION_OFF_HOURS` | Admin action xảy ra ngoài giờ làm việc | `workStartHour`, `workEndHour` | user, device, timestamp, location |
| 7 | `TOKEN_REFRESH_STORM` | Quá nhiều `TOKEN_REFRESH` liên tục, có thể vòng lặp lỗi/token abuse | `windowSeconds`, `maxRefresh` | user, app, số token refresh |
| 8 | `ACCESS_WITHOUT_LOGIN` | Có access/download/admin nhưng trước đó không thấy login hợp lệ | `lookbackSeconds` | user, device, event đáng nghi |
| 9 | `FAILED_LOGIN_FROM_MANY_DEVICES` | Nhiều device cùng failed login vào một user | `windowSeconds`, `maxDevices` | user bị nhắm tới, danh sách device |
| 10 | `UNKNOWN_FIELD_CLUSTER` | Nhiều event có `UNKNOWN_*` tập trung vào cùng user/device/resource | `windowSeconds`, `maxUnknownEvents` | entity liên quan, số lỗi UNKNOWN |
| 11 | `LOCATION_DEVICE_MISMATCH` | Cùng device nhưng location đổi liên tục quá nhanh | `windowSeconds`, `maxSwitches` | device, chuỗi location |
| 12 | `RARE_RESOURCE_ACCESS` | User truy cập resource mà rất ít user từng truy cập | `minGlobalUsers`, `lookbackDays` | user, resource hiếm, thời điểm |

---

### 5.1. `NEW_DEVICE_FOR_USER` – User dùng thiết bị mới

Luật này thực tế và dễ giải thích. Với mỗi user, hệ thống lưu danh sách device đã từng xuất hiện trong giai đoạn trước. Nếu trong khoảng truy vấn xuất hiện device chưa từng đi kèm user đó, hệ thống đánh dấu là đáng chú ý.

- Tham số:
  - `lookbackDays`: số ngày dùng để xem lịch sử cũ.
  - `riskBoostIfOffHour`: có tăng điểm nếu thiết bị mới xuất hiện ngoài giờ hay không.
- Điều kiện nghi vấn:
  - `deviceID` chưa từng xuất hiện với `userID` trong lịch sử trước đó.
- Điểm cộng:
  - Nếu device mới + location mới + ngoài giờ làm việc thì tăng score.

Đây là luật nên đưa vào vì rất gần với các hệ thống bảo mật thực tế: đăng nhập từ thiết bị mới luôn là tín hiệu đáng chú ý.

---

### 5.2. `SHARED_DEVICE_BY_MANY_USERS` – Một device dùng bởi quá nhiều user

Một device bình thường thường thuộc về một hoặc vài user. Nếu một device được nhiều user đăng nhập trong thời gian ngắn, có thể là máy công cộng, máy bị compromise, hoặc dữ liệu giả/lỗi.

- Tham số:
  - `windowSeconds`.
  - `maxUsersPerDevice`.
- Cách phát hiện:
  - Gom event theo `deviceID`.
  - Trong mỗi cửa sổ thời gian, đếm số `userID` khác nhau.
- Output:
  - device đáng nghi, danh sách user, thời gian bắt đầu/kết thúc.

---

### 5.3. `RESOURCE_SUDDEN_HOTSPOT` – Resource đột nhiên bị truy cập dồn dập

Nếu một resource bình thường ít được truy cập nhưng trong một khoảng ngắn lại nhận rất nhiều lượt từ nhiều user/device, có thể là dấu hiệu dò quét hoặc dữ liệu nhạy cảm bị phát tán.

- Tham số:
  - `windowSeconds`.
  - `maxAccess`.
  - `maxUsers`.
- Cách phát hiện:
  - Với mỗi resource, đếm số event `ACCESS`/`DOWNLOAD` trong cửa sổ thời gian.
  - Đếm thêm số user/device khác nhau.
- Output:
  - resource, số lượt, số user, số device, top user liên quan.

---

### 5.4. `DOWNLOAD_BURST` – Download dồn dập

Luật này rất dễ hiểu và có tính thực tế cao: download quá nhiều trong thời gian ngắn thường đáng nghi hơn access thông thường.

- Tham số:
  - `windowSeconds`.
  - `maxDownloads`.
- Cách phát hiện:
  - Đếm số event `DOWNLOAD` theo user hoặc device trong cửa sổ thời gian.
- Có thể tăng score nếu:
  - xảy ra ngoài giờ;
  - xảy ra sau `ADMIN_ACTION`;
  - download nhiều resource khác nhau;
  - location là location mới của user.

---

### 5.5. `ACCESS_WITHOUT_LOGIN` – Truy cập nhưng không có login trước đó

Trong thực tế, nếu user có `ACCESS`, `DOWNLOAD`, `ADMIN_ACTION` nhưng trong một khoảng trước đó không có `LOGIN` hoặc `TOKEN_REFRESH`, đây có thể là phiên bất thường hoặc log thiếu.

- Tham số:
  - `lookbackSeconds`.
- Cách phát hiện:
  - Với mỗi event quan trọng, nhìn ngược lại trong cùng user/device/app.
  - Nếu không thấy `LOGIN` hoặc `TOKEN_REFRESH`, đánh dấu.
- Output:
  - user, device, app, event, timestamp, lý do.

---

### 5.6. `TOKEN_REFRESH_STORM` – Token refresh quá nhiều

Nếu `TOKEN_REFRESH` xuất hiện liên tục với tần suất rất cao, có thể là lỗi vòng lặp của ứng dụng hoặc hành vi cố giữ phiên bất thường.

- Tham số:
  - `windowSeconds`.
  - `maxRefresh`.
- Cách phát hiện:
  - Đếm `TOKEN_REFRESH` theo user/app/device.
- Output:
  - user, app, device, số lần refresh.

---

### 5.7. `UNKNOWN_FIELD_CLUSTER` – Cụm dữ liệu lỗi tập trung

Giữa kì đã có cơ chế gán `UNKNOWN_*`. Cuối kì có thể tận dụng chính dữ liệu lỗi này để phát hiện bất thường.

Ý tưởng:

- Một vài dòng lỗi là bình thường.
- Nhưng nếu rất nhiều dòng `UNKNOWN_USER`, `UNKNOWN_DEVICE`, `UNKNOWN_LOCATION` tập trung trong cùng một khoảng thời gian hoặc cùng một resource, có thể là dữ liệu bị phá, giả lập lỗi hoặc có nguồn log không đáng tin.

- Tham số:
  - `windowSeconds`.
  - `maxUnknownEvents`.
- Output:
  - loại UNKNOWN, số lượng, entity liên quan.

Đây là ý tưởng hay vì tận dụng trực tiếp phần xử lý dữ liệu bẩn đã có ở giữa kì.

---

### 5.8. `RARE_RESOURCE_ACCESS` – Truy cập tài nguyên hiếm

Nếu một resource chỉ có rất ít user từng truy cập nhưng đột nhiên một user mới truy cập, có thể đánh dấu mức độ chú ý.

- Tham số:
  - `minGlobalUsers`: ngưỡng xác định resource hiếm.
  - `lookbackDays`.
- Cách phát hiện:
  - Đếm tổng số user từng truy cập mỗi resource.
  - Resource có số user nhỏ hơn ngưỡng được xem là resource hiếm.
  - Nếu user mới truy cập resource hiếm, trả về cảnh báo.

---

## 6. Cách nhập tham số và truy vấn kết quả

Báo cáo nên có một bảng tham số chung để thầy thấy hệ thống linh hoạt.

| Nhóm luật | Tham số | Ý nghĩa | Giá trị mặc định đề xuất |
|---|---|---|---|
| Time window | `windowSeconds` | Kích thước cửa sổ thời gian | 300 / 600 / 3600 |
| Device | `maxDevices` | Số device tối đa của user trong window | 3 |
| Failed login | `maxFailedLogin` | Số lần login thất bại tối đa | 5 |
| Resource | `maxDistinctResources` | Số resource khác nhau tối đa | 20 |
| Working hours | `workStartHour`, `workEndHour` | Khung giờ làm việc | 8, 18 |
| Location | `maxCountries`, `maxSwitches` | Ngưỡng đổi quốc gia/vị trí | 2, 3 |
| Session | `maxSessionDuration` | Thời lượng phiên tối đa | 8 giờ |
| Dormant | `dormantDays`, `burstEventThreshold` | Im lặng lâu rồi hoạt động mạnh | 7 ngày, 20 event |
| Download | `maxDownloads` | Số download tối đa trong window | 10 |
| Unknown | `maxUnknownEvents` | Số event lỗi tối đa | 20 |

---

## 7. Gợi ý giao diện phần bất thường

Nên thêm một mục trong báo cáo tên là **Giao diện phát hiện bất thường**.

Giao diện nên có các phần:

1. Chọn dataset.
2. Chọn nhóm luật:
   - Rule theo ngưỡng.
   - Rule theo hành vi.
   - Rule theo session.
   - Rule nâng cao.
   - Rule tự đề xuất.
3. Nhập tham số:
   - window time;
   - ngưỡng số lượng;
   - giờ làm việc;
   - khoảng thời gian scan.
4. Nút chạy:
   - “Chạy luật đang chọn”.
   - “Chạy tất cả luật”.
5. Bảng kết quả:
   - thời gian;
   - user;
   - device;
   - app;
   - resource;
   - event;
   - loại bất thường;
   - lý do;
   - score.
6. Có badge màu:
   - đỏ: nguy hiểm cao;
   - cam: đáng chú ý;
   - vàng: nghi vấn nhẹ.

---

## 8. Gợi ý cách chạy nên ghi trong báo cáo

Không cần lặp lại dài như báo cáo giữa kì. Chỉ cần ghi phần mới.

### 8.1. Chuẩn bị dữ liệu

```text
Đặt file CSV cần phân tích vào thư mục:
24120117/data/
```

File CSV vẫn giữ 7 cột:

```csv
user_id,device_id,app_id,resource_id,event_type,location,timestamp
```

### 8.2. Chạy chương trình

```powershell
cd 24120117
.\release\24120117.exe
```

Sau đó mở trình duyệt:

```text
http://localhost:8080
```

### 8.3. Quy trình sử dụng chức năng phát hiện bất thường

1. Chọn dataset.
2. Nhấn **Nạp dữ liệu**.
3. Mở tab **Anomaly Detection**.
4. Chọn khoảng thời gian cần phân tích.
5. Chọn luật hoặc chọn **Run All Rules**.
6. Nhập các ngưỡng mong muốn.
7. Nhấn **Scan**.
8. Xem bảng kết quả và lý do cảnh báo.
9. Nếu có hỗ trợ xuất file, nhấn **Export CSV/JSON**.

---

## 9. Giải thuật và độ phức tạp nên ghi

Không cần trình bày lại MergeSort, HashTable, Binary Search quá dài. Chỉ cần ghi cách các luật tận dụng dữ liệu đã sort.

### 9.1. Ý tưởng chung

- Dữ liệu sau khi nạp đã được sort theo timestamp.
- Mỗi truy vấn bất thường nhận `startTime`, `endTime`.
- Dùng binary search tìm đoạn bản ghi nằm trong khoảng thời gian.
- Chỉ quét đoạn đó thay vì quét toàn bộ dataset nếu người dùng chọn khoảng thời gian hẹp.
- Với từng luật, dùng bảng đếm tạm theo user/device/resource/app/location.

### 9.2. Bảng độ phức tạp đề xuất

| Chức năng | Cách làm | Độ phức tạp |
|---|---|---|
| Tìm đoạn thời gian | Binary Search trên mảng đã sort | `O(log n)` |
| Quét luật trong đoạn | Duyệt tuyến tính `k` bản ghi | `O(k)` |
| Đếm theo user/device/resource | HashTable tự cài | `O(1)` trung bình mỗi event |
| Dựng session | Duyệt event theo user/device/app | `O(k)` đến `O(k log k)` tùy cách gom |
| Chạy toàn bộ luật | Quét nhiều lượt hoặc gom chung một lượt | `O(r * k)` hoặc tối ưu `O(k)` với `r` luật |
| Sắp xếp kết quả theo score/time | MergeSort kết quả | `O(m log m)` |

Ký hiệu:

- `n`: tổng số bản ghi.
- `k`: số bản ghi trong khoảng thời gian truy vấn.
- `r`: số luật phát hiện bất thường.
- `m`: số kết quả bất thường trả về.

---

## 10. Thực nghiệm hiệu năng nên đưa vào báo cáo

Nên có ít nhất một bảng thực nghiệm. Nếu chưa có số thật thì để placeholder rồi điền sau.

| Dataset | Số dòng CSV | Số record hợp lệ | Thời gian nạp CSV | Thời gian nạp cache | RAM | Thời gian chạy toàn bộ luật | Ghi chú |
|---|---:|---:|---:|---:|---:|---:|---|
| Small | 10.000 | TODO | TODO ms | TODO ms | TODO MB | TODO ms | Test nhanh |
| Medium | 1.000.000 | TODO | TODO ms | TODO ms | TODO MB | TODO ms | Yêu cầu tối thiểu |
| Large | 5.000.000 | TODO | TODO ms | TODO ms | TODO MB | TODO ms | Kiểm thử mở rộng |
| Dirty data | TODO | TODO | TODO ms | TODO ms | TODO MB | TODO ms | Có dòng lỗi/trùng/UNKNOWN |

Nên có thêm bảng chất lượng dữ liệu:

| Chỉ số | Giá trị |
|---|---:|
| Tổng số dòng đọc được | TODO |
| Số dòng hợp lệ | TODO |
| Số dòng bị bỏ | TODO |
| Số dòng được gán `UNKNOWN_*` | TODO |
| Số bản ghi trùng được gộp | TODO |
| Số cảnh báo bất thường phát hiện được | TODO |

---

## 11. Khó khăn và hướng giải quyết nên ghi

Không nên lặp lại y nguyên khó khăn giữa kì. Nên ghi khó khăn mới.

### 11.1. Số lượng luật nhiều, dễ quét dữ liệu nhiều lần

- Khó khăn: Nếu mỗi luật quét toàn bộ dataset riêng, thời gian tăng theo số luật.
- Giải pháp: Chỉ quét đoạn thời gian cần phân tích; các luật có cùng kiểu đếm được gom chung trong một lượt duyệt nếu có thể.

### 11.2. Session không phải lúc nào cũng có LOGIN/LOGOUT rõ ràng

- Khó khăn: Log thực tế có thể thiếu `LOGIN` hoặc `LOGOUT`.
- Giải pháp: Dùng `idleTimeout` để tự đóng phiên tạm; đánh dấu session thiếu logout như một tín hiệu phụ, không crash chương trình.

### 11.3. Ngưỡng bất thường phụ thuộc môi trường

- Khó khăn: Ngưỡng đúng với dataset này có thể không đúng với dataset khác.
- Giải pháp: Cho phép nhập tham số từ giao diện, không hard-code; báo cáo rõ giá trị mặc định chỉ là đề xuất.

### 11.4. Dữ liệu bẩn có thể tạo cảnh báo giả

- Khó khăn: `UNKNOWN_*` có thể làm luật phát hiện sai.
- Giải pháp: Không bỏ qua hoàn toàn, nhưng gắn cờ `dataQualityWarning`; có một luật riêng phát hiện cụm `UNKNOWN_*` thay vì để dữ liệu lỗi phá các luật khác.

### 11.5. Kết quả bất thường có thể quá nhiều

- Khó khăn: Nếu trả toàn bộ cảnh báo lên web, giao diện chậm và khó đọc.
- Giải pháp: Sắp xếp theo `score`, giới hạn top N, phân trang, cho lọc theo loại rule/user/device/resource.

---

## 12. Đề xuất hướng phát triển thêm

Các ý này nên ghi ở cuối báo cáo, không cần triển khai hết.

- Thêm điểm rủi ro tổng hợp cho mỗi user/device/resource.
- Cho phép bật/tắt từng luật trên giao diện.
- Cho phép lưu cấu hình rule thành preset.
- Xuất kết quả bất thường ra CSV/JSON.
- Thêm dashboard “Top user đáng nghi”, “Top device đáng nghi”, “Top resource bị nhắm tới”.
- Thêm biểu đồ số cảnh báo theo thời gian.
- Thêm chế độ so sánh hai khoảng thời gian: hôm nay so với hôm qua, tuần này so với tuần trước.
- Hỗ trợ chạy rule theo batch để không khóa giao diện khi dataset rất lớn.
- Thêm bộ sinh dữ liệu kiểm thử có bất thường cố ý để demo.
- Tách module anomaly thành file riêng để dễ bảo trì: `AnomalyDetector.h/.cpp`, `SessionBuilder.h/.cpp`, `RuleConfig.h`.

---

## 13. Bảng đánh giá mức độ hoàn thành cuối kì nên có

| STT | Yêu cầu | Mức độ hoàn thành đề xuất ghi | Ghi chú |
|---:|---|---|---|
| 1 | Load dữ liệu lớn tối thiểu 1.000.000 dòng | TODO / 100% | Điền số thực nghiệm |
| 2 | User đăng nhập từ quá nhiều device | TODO / 100% | Có nhập `windowSeconds`, `maxDevices` |
| 3 | Login thất bại liên tục | TODO / 100% | Có nhập ngưỡng |
| 4 | Device truy cập quá nhiều resource | TODO / 100% | Có đếm distinct resource |
| 5 | Truy cập ngoài giờ làm việc | TODO / 100% | Có cấu hình giờ làm việc |
| 6 | User xuất hiện ở nhiều quốc gia không hợp lý | TODO / 100% | Có đếm location |
| 7 | User liên tục đổi vị trí địa lý | TODO / 100% | Có chuỗi location |
| 8 | Phiên làm việc dài bất thường | TODO / 100% | Có SessionBuilder |
| 9 | Nhiều phiên liên tục | TODO / 100% | Có đếm session/window |
| 10 | Chuỗi admin + download | TODO / 100% | Có score nguy hiểm |
| 11 | Failed login nhiều lần rồi success | TODO / 100% | Phần nâng cao |
| 12 | Im lặng lâu rồi hoạt động mạnh | TODO / 100% | Phần nâng cao |
| 13 | Các bất thường tự đề xuất thêm | TODO | Nên chọn 8–12 luật |
| 14 | Dữ liệu lỗi/trùng không làm crash | TODO / 100% | Kế thừa DataReader + UNKNOWN + dedup |
| 15 | Trả kết quả dưới 10 giây trên 1 triệu dòng | TODO | Cần có bảng benchmark |

---

## 14. Những phần nên tránh viết lại quá dài

Không nên dành nhiều trang cho:

- Giải thích lại `Vector<T>` hoạt động như thế nào.
- Giải thích lại `HashTable` separate chaining quá chi tiết.
- Giải thích lại cache `.dat` từ đầu.
- Hướng dẫn cài MSYS2 quá dài như giữa kì.
- Mô tả lại toàn bộ 3 query giữa kì.

Chỉ nên nhắc:

```text
Các thành phần này đã được trình bày trong báo cáo giữa kì. Trong phiên bản cuối kì, chúng được tái sử dụng để cung cấp dữ liệu đầu vào đã được chuẩn hóa và sắp xếp cho module phát hiện bất thường.
```

---

## 15. Bố cục báo cáo cuối kì hoàn chỉnh đề xuất

Có thể dùng mục lục này:

```text
1. Tổng quan nâng cấp cuối kì
   1.1. Mục tiêu nâng cấp
   1.2. Phạm vi báo cáo
   1.3. Những điểm mới so với giữa kì

2. Nền tảng kế thừa từ giữa kì
   2.1. Các cấu trúc dữ liệu đã có
   2.2. Luồng xử lý dữ liệu đã có
   2.3. Lý do không trình bày lại chi tiết

3. Thiết kế phần phát hiện bất thường
   3.1. Kiến trúc tổng thể phiên bản cuối kì
   3.2. AnomalyDetector
   3.3. SessionBuilder
   3.4. RuleConfig và tham số người dùng
   3.5. AnomalyResult và cách hiển thị kết quả
   3.6. Endpoint/API mới

4. Các luật phát hiện bất thường theo đề bài
   4.1. User dùng quá nhiều device
   4.2. Login thất bại liên tục
   4.3. Device truy cập nhiều resource
   4.4. Truy cập ngoài giờ
   4.5. User xuất hiện ở nhiều quốc gia
   4.6. User đổi vị trí liên tục
   4.7. Phiên dài bất thường
   4.8. Nhiều phiên liên tục
   4.9. Admin action + download
   4.10. Failed nhiều lần rồi login thành công
   4.11. Im lặng lâu rồi hoạt động mạnh

5. Các luật bất thường tự đề xuất
   5.1. New device for user
   5.2. New app for user
   5.3. Shared device by many users
   5.4. Resource sudden hotspot
   5.5. Download burst
   5.6. Admin action off-hours
   5.7. Token refresh storm
   5.8. Access without login
   5.9. Failed login from many devices
   5.10. Unknown field cluster

6. Giải thuật và độ phức tạp
   6.1. Tận dụng dữ liệu đã sort theo timestamp
   6.2. Sliding window / time window
   6.3. Đếm distinct entity bằng HashTable tự cài
   6.4. Dựng session
   6.5. Độ phức tạp tổng quát

7. Hướng dẫn cài đặt và sử dụng
   7.1. Chuẩn bị dataset
   7.2. Chạy chương trình
   7.3. Chạy chức năng anomaly detection
   7.4. Ý nghĩa các tham số
   7.5. Cách đọc kết quả

8. Thực nghiệm và đánh giá
   8.1. Dataset kiểm thử
   8.2. Thời gian nạp dữ liệu
   8.3. Thời gian chạy luật
   8.4. RAM sử dụng
   8.5. Số bất thường phát hiện được

9. Khó khăn và hướng giải quyết

10. Kết luận và hướng phát triển

11. Phụ lục: tham số mặc định và ví dụ output JSON/CSV
```

---

## 16. Ví dụ output nên đưa vào phụ lục

Có thể đưa ví dụ JSON để báo cáo rõ ràng hơn.

```json
{
  "ruleName": "FAILED_THEN_SUCCESS_LOGIN",
  "severity": "HIGH",
  "score": 90,
  "user": "U025",
  "device": "D011",
  "app": "APP003",
  "resource": "R018",
  "startTime": 1713225000,
  "endTime": 1713225900,
  "count": 6,
  "reason": "User failed login 5 times and then logged in successfully within 15 minutes"
}
```

Ví dụ bảng UI:

| Time | Rule | Severity | User | Device | Resource | Reason |
|---|---|---|---|---|---|---|
| 1713225900 | FAILED_THEN_SUCCESS_LOGIN | HIGH | U025 | D011 | R018 | Failed 5 times then success |
| 1713227000 | ADMIN_DOWNLOAD_CHAIN | HIGH | U008 | D002 | R077 | Admin action followed by 12 downloads |
| 1713230000 | NEW_DEVICE_FOR_USER | MEDIUM | U018 | D099 | - | First time this user used this device |

---

## 17. Gợi ý kết luận cuối báo cáo

```text
Phiên bản cuối kì của HALO đã mở rộng hệ thống từ một engine truy vấn log thành một engine phát hiện bất thường trên dữ liệu lớn. Hệ thống tận dụng các cấu trúc dữ liệu tự cài đặt ở giữa kì để xử lý dữ liệu hiệu quả, sau đó bổ sung các module phân tích theo ngưỡng, theo hành vi và theo phiên làm việc.

Các luật phát hiện bất thường được thiết kế theo hướng có thể cấu hình tham số, giúp hệ thống linh hoạt với nhiều loại dataset khác nhau. Bên cạnh các yêu cầu trong đề bài, đồ án cũng đề xuất thêm nhiều luật thực tế như thiết bị mới của user, device dùng bởi quá nhiều user, download dồn dập, token refresh bất thường và cụm dữ liệu UNKNOWN. Nhờ đó, HALO không chỉ đáp ứng yêu cầu phát hiện hành vi đáng ngờ mà còn có khả năng mở rộng thành một công cụ phân tích log bảo mật trực quan hơn trong tương lai.
```

---

## 18. Checklist trước khi nộp báo cáo

- [ ] Không lặp lại dài dòng báo cáo giữa kì.
- [ ] Có mục “những điểm mới so với giữa kì”.
- [ ] Có đủ 11 bất thường theo đề.
- [ ] Có ít nhất 8–12 bất thường tự đề xuất.
- [ ] Mỗi luật có tham số đầu vào rõ ràng.
- [ ] Mỗi luật có điều kiện nghi vấn rõ ràng.
- [ ] Có bảng endpoint/API hoặc mô tả giao diện chạy luật.
- [ ] Có bảng benchmark 1.000.000 dòng.
- [ ] Có ghi cách xử lý dữ liệu lỗi/trùng.
- [ ] Có ghi giải phóng bộ nhớ sau chức năng.
- [ ] Có hướng dẫn chạy ngắn gọn.
- [ ] Có hướng phát triển.
- [ ] Có ghi rõ AI chỉ hỗ trợ tham khảo/diễn đạt nếu có sử dụng.

