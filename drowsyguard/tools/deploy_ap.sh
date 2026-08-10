#!/bin/bash
# Script tự động đẩy code sang nhân Linux của Arduino UNO Q

# Thay đổi IP này thành IP của board UNO Q của bạn (VD: qua Wi-Fi hoặc IP USB nội bộ 192.168.7.2)
UNO_IP="192.168.7.2"
UNO_USER="root" # User mặc định của Debian trên UNO Q

echo "1. Đang đẩy code Python, Config và Model sang UNO Q..."
# Dùng rsync để đồng bộ toàn bộ thư mục dms-ap sang thư mục /home/root/drowsyguard/
rsync -avz --exclude '__pycache__' ../dms-ap/ $UNO_USER@$UNO_IP:/home/root/drowsyguard/dms-ap/

echo "2. Chạy thử chương trình qua SSH..."
# Kết nối SSH và chạy lệnh
ssh $UNO_USER@$UNO_IP "cd /home/root/drowsyguard/dms-ap/src && python3 drowsyguard/main.py"