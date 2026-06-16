1. Sau các câu lệnh if, for, while,... thì statement phải nằm trong {}
2. Khi đã cấp phát thì bắt buộc phải xoá bộ nhớ
3. Khai báo biến thì phải gán giá trị ban đầu và mỗi dòng chỉ có 1 biến với 1 kiểu dữ liệu, ví dụ như int i = 0; (đúng). int i, a; (sai)
4. Không được dùng: `vector`, `map`, `unordered_map`, `set`, thư viện đồ thị
5. Được dùng: `std::string`, các hàm đọc/ghi file
6. Chức năng nào thì nằm ở trong đó, ví dụ như các chức năng liên quan đến vector thì buộc nằm trong Vector.h, liên quan đến các hàm sort thì buộc bỏ trong Sort.h


