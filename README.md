# ADASmini

Mô hình ADAS thu nhỏ trên vi điều khiển STM32: đo khoảng cách phía trước bằng cảm biến siêu âm, cảnh báo bằng LED và màn hình OLED, đồng thời điều khiển động cơ để dừng/lùi khi có vật cản ở gần.

Toàn bộ driver (GPIO, Timer, I2C, OLED) được viết ở **mức thanh ghi** (register-level), không dùng HAL.

## Tính năng

- Đo khoảng cách phía trước bằng cảm biến siêu âm HC-SR04
- Ba mức cảnh báo hiển thị trên OLED SSD1306 128x64:

| Khoảng cách | Trạng thái | OLED | LED | Động cơ |
|---|---|---|---|---|
| < 50 cm | DANGER | `DANGER` | LED PA4 bật | Dừng 100 ms, sau đó chạy chế độ lùi |
| 50 – 100 cm | WARNING | `WARNING` | LED PA5 bật | Chạy bình thường |
| > 100 cm | SAFE | `SAFE` | Tắt | Chạy bình thường |

## Phần cứng

- Vi điều khiển: STM32 [ghi rõ tên board/chip, ví dụ: STM32F4xx]
- Cảm biến siêu âm HC-SR04
- Màn hình OLED SSD1306 128x64 giao tiếp I2C (địa chỉ 0x78 ở dạng 8-bit, tương đương 0x3C 7-bit)
- Mạch điều khiển động cơ: [ghi rõ, ví dụ: L298N]
- 2 LED báo hiệu
- Nguồn: [ghi rõ nguồn cấp cho vi điều khiển và cho động cơ]

## Sơ đồ nối chân

| Chức năng | Chân |
|---|---|
| HC-SR04 TRIG | PB8 |
| HC-SR04 ECHO | PB9 |
| Động cơ trái | PC0 |
| Động cơ phải | PC1 |
| OLED SCL (I2C1) | PB6 |
| OLED SDA (I2C1) | PB7 |
| LED DANGER | PA4 |
| LED WARNING | PA5 |

Lưu ý: ECHO của HC-SR04 xuất ra mức 5 V. Nếu chân vi điều khiển không chịu được 5 V thì cần cầu phân áp hoặc chân FT (5 V tolerant).

## Nguyên lý hoạt động

1. Phát xung 10 µs lên chân TRIG.
2. Đo độ rộng xung ở chân ECHO bằng TIM2 (đếm ở 1 MHz, tức 1 µs mỗi nhịp).
3. Khoảng cách (cm) = thời gian (µs) / 58.
4. So sánh với các ngưỡng 50 cm và 100 cm để chọn trạng thái, cập nhật OLED, LED và động cơ.
5. Lặp lại sau mỗi khoảng 100 ms.

## Cách build và nạp

1. Cài [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html).
2. Clone repo:
   ```bash
   git clone https://github.com/trantoan1103/ADASmini.git
   ```
3. Trong STM32CubeIDE: **File → Import → Existing Projects into Workspace**, chọn thư mục vừa clone.
4. Build (**Project → Build Project**) và nạp xuống board bằng ST-LINK.

## Cấu trúc thư mục

```
ADASmini/
├── Core/      # Mã nguồn chính (main.c, ...)
├── Drivers/   # Thư viện CMSIS/HAL do CubeMX sinh ra
└── *.ioc      # File cấu hình CubeMX
```

## Vấn đề đã biết / việc cần làm

- [ ] Hàm đọc khoảng cách trả về giá trị âm khi timeout, hiện đang bị hiểu là "DANGER". Cần xử lý riêng trường hợp lỗi cảm biến.
- [ ] Timeout đang dùng vòng lặp đếm thay vì dùng timer nên phụ thuộc tốc độ CPU.
- [ ] Chỉ cập nhật OLED khi trạng thái thay đổi để giảm thời gian chiếm I2C trong vòng lặp chính.
- [ ] Gộp hai hàm delay trùng chức năng.
- [ ] Bổ sung ảnh/video demo.

## Tác giả

[trantoan1103](https://github.com/trantoan1103)
