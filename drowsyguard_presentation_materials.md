# TÀI LIỆU SẢN PHẨM: DROWSY GUARD

Dưới đây là nội dung đề xuất cho **Catalog (Tờ rơi/Brochure giới thiệu)** và **Kịch bản thuyết trình (Presentation Script)** cho dự án DrowsyGuard.

---

## PHẦN 1: NỘI DUNG CATALOG (BROCHURE)

**[Trang bìa]**
# DROWSY GUARD
**Hệ thống Cảnh báo Buồn ngủ Thông minh Ứng dụng Edge-AI trên Arduino UNO Q**
*Giải pháp bảo vệ tính mạng tài xế theo thời gian thực - Bảo mật, Độ trễ thấp, Hiệu quả cao.*
*Sản phẩm phát triển bởi đội ngũ MLIoT_Love50.*

**[Trang 2: Vấn đề & Giải pháp]**
### TẠI SAO CHỌN DROWSY GUARD?
Buồn ngủ khi lái xe (Micro-sleep) là một trong những nguyên nhân hàng đầu gây ra các vụ tai nạn giao thông nghiêm trọng. DrowsyGuard giải quyết triệt để vấn đề này thông qua công nghệ **Edge-AI** tiên tiến, theo dõi trạng thái tài xế liên tục mà không cần kết nối mạng.

* **🔒 Bảo mật & Quyền riêng tư tuyệt đối:** Xử lý 100% On-Device (trên thiết bị). Không có bất kỳ hình ảnh hay video nào của tài xế được gửi ra ngoài.
* **⚡ Tốc độ phản hồi chớp nhoáng (<100ms):** Can thiệp vật lý tức thời ngay khi phát hiện dấu hiệu buồn ngủ.
* **🚘 An toàn Thông minh (Smart Living):** Ngăn chặn tai nạn trước khi nó xảy ra, mang lại sự an tâm tuyệt đối trên mọi hành trình.

**[Trang 3: Tính năng Cốt lõi & Kiến trúc]**
### SỨC MẠNH TỪ KIẾN TRÚC "DUAL-BRAIN" (NÃO BỘ KÉP)
DrowsyGuard khai thác tối đa sức mạnh phần cứng của nền tảng **Arduino UNO Q**:
1. **Lõi Vision AI (Qualcomm Dragonwing QRB2210):** Chạy các mô hình học sâu (MediaPipe) để phân tích khuôn mặt:
   - Tỷ lệ nhắm mắt (PERCLOS)
   - Tần suất ngáp
   - Trạng thái gật gù của đầu
2. **Lõi Điều khiển (STM32U585 MCU):** Thực thi các can thiệp vật lý an toàn tức thì với độ trễ cực thấp.

### CAMERA KÉP NGÀY & ĐÊM
Hệ thống tích hợp camera RGB và Hồng ngoại (IR), đảm bảo khả năng theo dõi chính xác và liên tục trong mọi điều kiện ánh sáng của khoang lái (từ ban ngày chói lọi đến ban đêm tối tăm).

**[Trang 4: Cơ chế Cảnh báo 4 Cấp độ]**
### HỆ THỐNG CẢNH BÁO ĐA TẦNG
* **🟢 Cấp độ 0 (Bình thường):** Theo dõi âm thầm, không làm phiền tài xế.
* **🟡 Cấp độ 1 (Cảnh báo Sớm):** Phát âm thanh bíp nhẹ và đèn LED vàng báo hiệu khi tài xế có dấu hiệu mệt mỏi.
* **🟠 Cấp độ 2 (Ngủ gật):** Kích hoạt chuông báo động cường độ cao, rung ghế lái (Haptic) và bật rơ-le quạt gió làm mát để đánh thức.
* **🔴 Cấp độ 3 (Nguy hiểm):** Bật đèn khẩn cấp (Hazard), phát tín hiệu giảm tốc độ xe, đồng thời gửi tin nhắn SMS/GPS cầu cứu qua dữ liệu viễn thông (Telemetry).

---

## PHẦN 2: KỊCH BẢN THUYẾT TRÌNH (PITCH SCRIPT)

**Thời lượng dự kiến:** 3 - 5 phút
**Đối tượng:** Ban giám khảo cuộc thi Qualcomm FutureMakers / Nhà đầu tư.

**[Slide 1: Tiêu đề & Giới thiệu]**
* **MC / Người thuyết trình:** 
"Xin chào ban giám khảo và toàn thể quý vị. Chúng tôi là đội MLIoT_Love50 đến từ PTN Machine Learning & IoT - Đại học Bách Khoa TP.HCM. Hôm nay, chúng tôi vô cùng tự hào mang đến cuộc thi Qualcomm FutureMakers 2026 một giải pháp có thể cứu sống hàng ngàn sinh mạng mỗi năm trên các nẻo đường: **Drowsy Guard - Hệ thống Cảnh báo Buồn ngủ Thông minh bằng Edge-AI.**"

**[Slide 2: Nêu Vấn đề]**
* **Người thuyết trình:** 
"Quý vị có biết, 'giấc ngủ trắng' hay micro-sleep kéo dài chỉ 2-3 giây ở tốc độ 80km/h có thể khiến xe di chuyển một quãng đường dài bằng một sân bóng đá trong trạng thái hoàn toàn mất kiểm soát. Các giải pháp giám sát tài xế hiện nay trên thị trường thường gặp phải 2 rào cản lớn: Thứ nhất là xâm phạm quyền riêng tư khi liên tục gửi hình ảnh lên cloud; Thứ hai là độ trễ quá cao để có thể can thiệp kịp thời. Chúng tôi tạo ra DrowsyGuard để xóa bỏ những rào cản đó."

**[Slide 3: Giải pháp DrowsyGuard & Công nghệ Cốt lõi]**
* **Người thuyết trình:** 
"Vậy DrowsyGuard giải quyết bài toán này như thế nào?
Giải pháp của chúng tôi là một hệ thống 100% On-device, chạy hoàn toàn trên bo mạch **Arduino UNO Q**. Chúng tôi thiết kế một kiến trúc 'Não bộ kép' (Dual-Brain) độc đáo:
- Khối xử lý trung tâm là chip **Qualcomm Dragonwing QRB2210** đảm nhiệm việc chạy các mô hình AI thị giác máy tính như MediaPipe để theo dõi mắt, miệng và tư thế đầu qua module camera RGB và Hồng ngoại (IR) bất kể ngày đêm.
- Khối vi điều khiển **STM32U585** sẽ ngay lập tức nhận lệnh và kích hoạt các phản ứng vật lý với độ trễ chưa tới 100ms.
Đặc biệt, vì AI xử lý trực tiếp trên thiết bị gốc (Edge AI), chúng tôi cam kết **KHÔNG một khung hình nào của tài xế bị rò rỉ ra bên ngoài**, đảm bảo quyền riêng tư tuyệt đối."

**[Slide 4: Cơ chế Cảnh báo Hành động]**
* **Người thuyết trình:** 
"Không chỉ dừng lại ở việc nhận diện, DrowsyGuard sở hữu Hệ thống Cảnh báo Đa tầng gồm 4 cấp độ:
- Ở Cấp độ 1, khi mới phát hiện mệt mỏi, hệ thống chỉ nhắc nhở nhẹ nhàng bằng LED và âm bíp.
- Nhưng khi tài xế chuyển sang Cấp độ 2 (Ngủ gật), ghế ngồi sẽ rung lắc mạnh, còi báo động réo lên và quạt gió sẽ thốc thẳng vào mặt để đánh thức.
- Trong trường hợp xấu nhất ở Cấp độ 3, hệ thống sẽ tự động bật đèn khẩn cấp của xe, phát tín hiệu giảm tốc và gửi ngay toạ độ GPS cứu hộ."

**[Slide 5: Tổng kết]**
* **Người thuyết trình:** 
"Tóm lại, DrowsyGuard không chỉ là một sản phẩm phần mềm, nó là một hệ thống tích hợp phần cứng - phần mềm hoàn chỉnh, bảo mật cao, không phụ thuộc vào Internet và sẵn sàng để thương mại hóa. Với sức mạnh của công nghệ Qualcomm và Arduino UNO Q, DrowsyGuard sẵn sàng đồng hành và bảo vệ mọi chuyến đi.
Xin cảm ơn ban giám khảo đã lắng nghe!"
