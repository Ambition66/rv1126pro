import cv2
import os

os.makedirs("images", exist_ok=True)  # 确保目录存在
cap = cv2.VideoCapture('palace.mp4')

count, num, step = 1, 0, 30

while cap.isOpened():
    ret, frame = cap.read()
    if not ret: break
    
    if num % step == 0:
        cv2.imwrite(f"images/frame{count}.jpg", frame)
        count += 1
    num += 1

cap.release()