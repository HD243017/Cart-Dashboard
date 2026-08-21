import time
import requests
import numpy as np
import cv2
from PyQt5.QtCore import QThread, pyqtSignal

class VideoThread(QThread):
    # (이미지 프레임, LED 감지 결과 dict) 시그널 전송
    data_received_signal = pyqtSignal(np.ndarray, dict, list)

    def __init__(self, base_url="http://192.168.0.83"):
        super().__init__()
        self.base_url = base_url.rstrip('/')
        self.running = True

    def run(self):
        while self.running:
            try:
                # 1. 캡처된 이미지 가져오기
                img_url = f"{self.base_url}/image?t={int(time.time() * 1000)}"
                img_resp = requests.get(img_url, timeout=3)
                
                # 2. 감지 데이터(JSON) 가져오기
                data_url = f"{self.base_url}/data"
                data_resp = requests.get(data_url, timeout=3)

                if img_resp.status_code == 200 and data_resp.status_code == 200:
                    # JPEG -> OpenCV BGR Image 변환
                    img_array = np.frombuffer(img_resp.content, dtype=np.uint8)
                    cv_img = cv2.imdecode(img_array, cv2.IMREAD_COLOR)

                    # Score 0.9 이상인 객체별 카운팅
                    detections = data_resp.json()
                    counts = {'r1': 0, 'g1': 0, 'y1': 0}
                    
                    valid_detections = []

                    for item in detections:
                        label = item.get('label')
                        score = item.get('value', 0.0)
                        if score >= 0.75 and label in counts:
                            valid_detections.append(item)
                            counts[label] += 1

                    if cv_img is not None:
                        self.data_received_signal.emit(cv_img, counts, valid_detections)

            except Exception as e:
                pass

            for _ in range(50):
                if not self.running:
                    break
                time.sleep(0.1)

    def stop(self):
        self.running = False
        self.wait()
