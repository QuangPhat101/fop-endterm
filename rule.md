## 1. YÊU CẦU KỸ THUẬT & VẬN HÀNH

### 1.1 Từ đề bài (bắt buộc)
- Load tối thiểu **100.000.000 dòng** mà không crash
- Trả kết quả dưới **10 giây**
- Sau mỗi chức năng, **giải phóng bộ nhớ** hoàn toàn
- Xử lý được dữ liệu **không hợp lệ** và **dòng trùng lặp**
- Bắt buộc dùng: `struct`, con trỏ, cấp phát động (array/linked list)
- **Không được dùng**: `vector`, `map`, `unordered_map`, `set`, thư viện đồ thị
- Được dùng: `std::string`, các hàm đọc/ghi file

### 1.2 Từ note của bạn
- Giữ nguyên **tính năng hiện giờ đang có**, nâng cấp lên (không làm lại từ đầu)
- **Port**: không dùng 8080, dùnng port nào đó ít thông dụng (ít bị đụng hàng)
- **Giao diện bất thường**: cho người dùng tự chọn loại bất thường muốn kiểm tra; nếu bất thường có tham số (ngưỡng, khoảng thời gian...) thì mới hỏi người dùng nhập
- Phát hiện bất thường chạy trên **toàn bộ database**, không phải từng query đơn lẻ
- **Kết quả**: in ra màn hình hoặc lưu file (nên hỏi người dùng chọn)

### 1.3 Xử lý dữ liệu đầu vào (cần lưu ý)
- Khoảng thời gian truy vấn không hợp lệ: `start > end` → báo lỗi, không crash
- Dòng thiếu trường, sai định dạng timestamp → bỏ qua, ghi log lỗi
- Dữ liệu trùng lặp hoàn toàn (cùng tất cả 7 trường) → deduplicate khi load

---

## 2. CÁC BẤT THƯỜNG CẦN PHÁT HIỆN

###  NHÓM A — Bắt buộc theo đề

#### A1. Ngưỡng — Đăng nhập từ quá nhiều thiết bị
- **Logic**: Một `user_id` có > N `device_id` phân biệt trong cửa sổ T giây
- **Tham số**: N (số device), T (cửa sổ thời gian) — cho người dùng nhập
- **Dữ liệu cần**: `LOGIN` event, group by user

#### A2. Ngưỡng — Login thất bại liên tục
- **Logic**: Đếm chuỗi `FAILED_LOGIN` liên tiếp của cùng `user_id`, vượt ngưỡng N lần
- **Tham số**: N (số lần thất bại tối đa)
- **Mở rộng**: Xem thêm A14 (thất bại rải rác theo tuần)

#### A3. Ngưỡng — Thiết bị truy cập quá nhiều resource
- **Logic**: Một `device_id` có > N `resource_id` phân biệt trong T giây
- **Tham số**: N, T

#### A4. Ngưỡng — Truy cập ngoài giờ làm việc
- **Logic**: `timestamp % 86400` chuyển sang giờ UTC; flag nếu ngoài [8h–18h]
- **Lưu ý**: Cần điều chỉnh timezone theo `location` (VN=UTC+7, US=UTC-5, JP=UTC+9...)
- **Tham số**: Giờ bắt đầu / kết thúc ca làm việc (mặc định 8–18)

#### A5. Hành vi địa lý — Xuất hiện ở nhiều quốc gia bất hợp lý
- **Logic**: Hai event liên tiếp của cùng user ở 2 quốc gia X → Y. Nếu hai quốc gia không có biên giới chung, dùng tọa độ đại diện và công thức Haversine để tính khoảng cách đường chim bay
- **Thời gian tối thiểu**: `khoảng cách / 900 km/h + 2 giờ` cho thủ tục sân bay, cất cánh và hạ cánh
- **Xét biên giới**: Các cặp quốc gia có biên giới chung trong tập dữ liệu được xem là khả thi theo yêu cầu, không phát cảnh báo A5
- **Kết quả**: Mỗi cảnh báo nhóm đúng 2 record liên tiếp, ghi quốc gia đi/đến, khoảng cách km, thời gian thực tế và thời gian tối thiểu

#### A6. Hành vi địa lý — Đổi vị trí địa lý liên tục
- **Logic**: Đếm số lần thay đổi `location` của user trong 1 ngày vượt ngưỡng N lần
- **Tham số**: N

#### A7. Phiên làm việc — Phiên dài bất thường
- **Logic**: `LOGOUT.timestamp - LOGIN.timestamp` > T giây (vd: 12h = 43200s)
- **Tham số**: T

#### A8. Phiên làm việc — Tạo nhiều phiên liên tục
- **Logic**: Đếm số cặp `LOGIN` của cùng user trong cửa sổ T giây vượt ngưỡng N
- **Tham số**: N, T

#### A9. Phiên làm việc — Chuỗi hành động nguy hiểm
- **Logic**: Trong 1 phiên (LOGIN → LOGOUT): có `ADMIN_ACTION` + N lần `DOWNLOAD` liên tiếp sau đó
- **Tham số**: N (số lần DOWNLOAD tối thiểu để coi là đáng ngờ)

---

### NHÓM B — Nâng cao theo đề

#### B1. Cố đăng nhập (Brute force cuối cùng thành công)
- **Logic**: N lần `FAILED_LOGIN` liên tiếp của cùng user, sau đó kết thúc bằng `LOGIN` thành công
- **Tham số**: N (số lần thất bại tối thiểu trước khi thành công)

#### B2. Im lặng rồi đột ngột hoạt động mạnh
- **Logic**: User không có event trong khoảng T_silence giây, sau đó trong T_burst giây có > N event
- **Tham số**: T_silence, T_burst, N

---

### NHÓM C — Bất thường từ ý tưởng của bạn (điểm cộng)

#### C1. Đăng nhập kép — Phiên chưa đóng đã có đăng nhập mới
- **Logic**: User đã có `LOGIN` chưa có `LOGOUT` tương ứng, mà xuất hiện thêm `LOGIN` mới (có thể từ device khác)
- **Ý nghĩa**: Chiếm phiên, session hijacking
- **Độ khó implement**: Trung bình (cần track trạng thái phiên per user)

#### C2. Đăng xuất rỗng (Logout không có phiên)
- **Logic**: `LOGOUT` xuất hiện mà không có `LOGIN` tương ứng trước đó, hoặc user đã `LOGOUT` rồi lại `LOGOUT` lần nữa
- **Ý nghĩa**: Dữ liệu giả mạo, replay attack, lỗi đồng bộ

#### C3. Đa nhiệm bất khả thi trên cùng thiết bị
- **Logic**: Cùng `user_id` + `device_id`, tại cùng timestamp (hoặc chênh lệch < N giây) nhưng dùng `app_id` và `resource_id` khác nhau
- **Ý nghĩa**: Một thiết bị không thể thực sự chạy 2 phiên độc lập cùng lúc — có thể bị giả mạo token
- **Tham số**: Ngưỡng thời gian chồng lấp (giây)

#### C4. Login thất bại rải rác (Low-and-slow brute force)
- **Logic**: Cùng `user_id` bị `FAILED_LOGIN` N lần nhưng phân bố rải rác (mỗi lần cách nhau > T giây, trong vòng W ngày)
- **Ý nghĩa**: Hacker thử mỗi ngày 1 password để tránh bị lock
- **Tham số**: N (tổng số lần), T (khoảng cách tối thiểu giữa 2 lần), W (cửa sổ tổng)
- **Phân biệt với A2**: A2 là liên tiếp nhanh, C4 là rải rác chậm

#### C5. Đăng nhập từ vị trí hoàn toàn mới
- **Logic**: Toàn bộ lịch sử login của user chỉ xuất hiện ở tập quốc gia {Q1, Q2,...}, bỗng nhiên có 1 login từ quốc gia hoàn toàn mới Qx chưa từng xuất hiện
- **Ý nghĩa**: Account bị đánh cắp và dùng từ nơi khác
- **Lưu ý**: Khác A5 (A5 xét tốc độ di chuyển, C5 xét lịch sử quốc gia)

#### C6. User + Device login/logout liên tiếp trùng lặp
- **Logic**: Cùng `user_id` + `device_id` xuất hiện 2 `LOGIN` liên tiếp (không có LOGOUT xen giữa) hoặc 2 `LOGOUT` liên tiếp
- **Ý nghĩa**: Dữ liệu không nhất quán, có thể là replay/inject log

---

### NHÓM D — Ý tưởng mới đề xuất thêm (điểm cộng cao)

#### D1. Token Refresh storm (Bot/Script detection)
- **Logic**: User có > N lần `TOKEN_REFRESH` trong T giây mà không có `ACCESS` xen giữa
- **Ý nghĩa**: Script tự động refresh token để duy trì phiên — dấu hiệu của bot hoặc malware
- **Tham số**: N, T

#### D2. Tốc độ thao tác bất thường — Speed anomaly
- **Logic**: Hai event liên tiếp của cùng user cách nhau < T_min giây (vd: < 1s), lặp lại > N lần
- **Ý nghĩa**: Con người không thể thao tác nhanh như vậy → bot/script
- **Tham số**: T_min, N (số lần lặp để confirm)

#### D3. Thiết bị dùng chung bất thường
- **Logic**: Một `device_id` xuất hiện với > N `user_id` khác nhau trong T giây
- **Ý nghĩa**: Thiết bị bị chia sẻ tài khoản, hoặc device bị compromise và dùng để test nhiều credential
- **Tham số**: N, T

#### D4. App lạ kết hợp với hành động nguy hiểm
- **Logic**: User dùng `app_id` mà họ chưa từng dùng trong lịch sử, và ngay trong phiên đó thực hiện `ADMIN_ACTION` hoặc `DOWNLOAD`
- **Ý nghĩa**: Cài tool lạ để khai thác — dấu hiệu của insider threat hoặc malware

#### D5. Resource bị tấn công đồng thời — Concurrent storm
- **Logic**: Cùng `resource_id` bị truy cập bởi > N user/device khác nhau trong T giây
- **Ý nghĩa**: Coordinated attack, DDoS nội bộ, hoặc credential sharing để cùng truy cập tài nguyên nhạy cảm
- **Tham số**: N, T

#### D6. Credential stuffing — Một device thử nhiều tài khoản
- **Logic**: Cùng `device_id` xuất hiện `FAILED_LOGIN` với > N `user_id` khác nhau trong T giây
- **Ý nghĩa**: Một máy thử đăng nhập nhiều tài khoản — tấn công nhồi thông tin đăng nhập
- **Phân biệt với A2**: A2 là 1 user bị thất bại nhiều lần, D6 là 1 device thất bại trên nhiều user khác nhau
- **Tham số**: N, T

#### D7. Privilege escalation → Data exfiltration
- **Logic**: Trong 1 phiên, user thực hiện đúng chuỗi: `ACCESS` → `ADMIN_ACTION` trên resource X → `DOWNLOAD` resource X (cùng resource_id)
- **Ý nghĩa**: Leo thang quyền rồi rút dữ liệu ngay lập tức — khác A9 ở chỗ nhắm vào cùng 1 resource cụ thể
- **Tham số**: Khoảng thời gian tối đa giữa ADMIN_ACTION và DOWNLOAD (giây)

#### D8. Phân bố hoạt động đều bất thường — Robot timing
- **Logic**: Trong 1 ngày, tính hệ số biến thiên `CV = độ lệch chuẩn / khoảng cách trung bình` của các khoảng cách event theo từng user. Cảnh báo khi CV rất thấp và ngày đó có đủ số event tối thiểu
- **Ý nghĩa**: Con người thao tác không đều — bot thì thao tác đúng giờ như cron job
- **Tham số**: Số event tối thiểu trong ngày, ngưỡng CV tối đa (%)
- **Cải tiến**: Dùng CV thay cho độ lệch chuẩn tuyệt đối để so sánh công bằng giữa bot chạy mỗi vài giây và bot chạy mỗi vài giờ

#### D9. Tỉ lệ event_type bất thường
- **Logic**: Với một user, tỉ lệ các loại event quá bất thường so với baseline của toàn hệ thống. Ví dụ: `LOGIN / ACCESS ratio` quá cao (login nhiều nhưng không access gì) hoặc `DOWNLOAD / ACCESS` quá cao
- **Ý nghĩa**: Hành vi không giống người dùng thực — có thể là scraper, hoặc account bị dùng sai mục đích

#### D10. Location tập trung FAILED_LOGIN bất thường
- **Logic**: Trong cửa sổ thời gian T, một location có đủ số event tối thiểu và tỉ lệ `FAILED_LOGIN / tổng event` vượt ngưỡng phần trăm cấu hình
- **Ý nghĩa**: Có thể là nguồn tấn công theo khu vực, proxy/VPN bị lạm dụng hoặc sự cố xác thực cục bộ
- **Tham số**: Cửa sổ T, số event tối thiểu, tỉ lệ FAILED_LOGIN tối thiểu (%)
- **Cải tiến**: Không cảnh báo chỉ vì 1–2 mẫu lỗi; mỗi đợt liên tục chỉ tạo một cảnh báo để tránh spam


---

## 3. BẢNG TỔNG HỢP ƯU TIÊN IMPLEMENT

| ID | Tên bất thường | Nhóm | Độ khó | Ấn tượng | Điểm kỳ vọng |
|----|----------------|-------|--------|----------|--------------|
| A1–A9 | Các bất thường bắt buộc | Đề bài | ★★★ | — | 9đ cơ bản |
| B1–B2 | Nâng cao theo đề | Đề bài | ★★★★ | — | +2đ |
| C1 | Phiên chưa đóng đã login mới | Bạn đề xuất | ★★★ | ★★★★ | +0.5–1đ |
| C3 | Đa nhiệm bất khả thi | Bạn đề xuất | ★★★ | ★★★★ | +0.5–1đ |
| C4 | Low-and-slow brute force | Bạn đề xuất | ★★★ | ★★★★★ | +0.5–1đ |
| C5 | Login từ quốc gia mới hoàn toàn | Bạn đề xuất | ★★ | ★★★★ | +0.5đ |
| D1 | Token Refresh storm | Đề xuất mới | ★★ | ★★★★ | +0.5–1đ |
| D2 | Speed anomaly (bot detection) | Đề xuất mới | ★★ | ★★★★★ | +0.5–1đ |
| D3 | Thiết bị dùng chung | Đề xuất mới | ★★ | ★★★ | +0.2–0.5đ |
| D6 | Credential stuffing | Đề xuất mới | ★★ | ★★★★★ | +0.5–1đ |
| D7 | Privilege escalation → Exfiltration | Đề xuất mới | ★★★ | ★★★★★ | +0.5–1đ |
| D8 | Robot timing | Đề xuất mới | ★★★★ | ★★★★★ | +0.5–1đ |
| D10 | Location tập trung FAILED_LOGIN | Đề xuất mới | ★★★ | ★★★★★ | +0.5–1đ |

---

## 4. GHI CHÚ THIẾT KẾ HỆ THỐNG

### Quản lý phiên làm việc
- Cần một cấu trúc `Session` để track trạng thái mỗi user: thời điểm LOGIN, LOGOUT, device đang dùng
- Khi load dữ liệu, sắp xếp theo `(user_id, timestamp)` để xử lý phiên tuần tự

### Timezone theo quốc gia (cho A4)
```
VN  → UTC+7    US  → UTC-5    JP  → UTC+9
KR  → UTC+9    SG  → UTC+8    CN  → UTC+8
DE  → UTC+1    FR  → UTC+1    UK  → UTC+0
AU  → UTC+10   CA  → UTC-5    IN  → UTC+5.5
BR  → UTC-3    RU  → UTC+3    TH  → UTC+7
```

### Thời gian di chuyển tối thiểu giữa các quốc gia (cho A5)
- Lưu một mảng tọa độ `(latitude, longitude)` đại diện cho mỗi quốc gia trong enum `location`
- Dùng Haversine để ước lượng khoảng cách đường chim bay giữa hai quốc gia
- Giả định vận tốc máy bay 900 km/h và cộng 2 giờ cho thủ tục/cất hạ cánh
- Danh sách cặp có biên giới trong tập quốc gia hiện tại: US–CA, VN–CN, CN–IN, CN–RU, DE–FR
- Đây là ước lượng tương đối ở cấp quốc gia, không thay thế dữ liệu sân bay/thành phố chính xác

### Output kết quả
- Hỏi người dùng: in ra màn hình hay lưu file
- Timestamp hiển thị theo UTC+0 ở định dạng ISO 8601
- Mỗi kết quả là một incident có thời gian bắt đầu/kết thúc, lý do và danh sách records bằng chứng
- Nếu lưu file: tên dạng `anomaly_<TYPE>_<UTC>_<milliseconds>.txt`
- Nội dung file dùng JSON Lines: hai dòng đầu là metadata bắt đầu bằng `#`, sau đó mỗi dòng JSON là một incident độc lập
- JSON Lines giúp đọc tuần tự file lớn, parse ổn định và không cần nạp toàn bộ file vào RAM
- Lịch sử hiển thị type, thời gian tạo UTC, kích thước file; hỗ trợ xem lại, xóa từng file và xóa toàn bộ
