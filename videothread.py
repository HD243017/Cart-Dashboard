import cv2
import numpy as np
from PyQt5.QtCore import QThread, pyqtSignal

# ==========================================
# 1. ESP32-CAM 영상 수신 백그라운드 스레드
# ==========================================

class VideoThread(QThread):
    change_pixmap_signal = pyqtSignal(np.ndarray)

    def __init__(self, stream_url="http://192.168.0.83:81/"): # 0: 웹캠, URL: "http://192.168.4.1:81/stream"
        super().__init__()
        self.stream_url = stream_url
        self._run_flag = True
        self.cap = None

    def run(self):
        # OpenCV VideoCapture 연결
        self.cap = cv2.VideoCapture(self.stream_url, cv2.CAP_FFMPEG)
        
        while self._run_flag:
            if not self.cap or not self.cap.isOpened():
                self.show_waiting_screen()
                self.msleep(1000)
                # 재연결 시도
                if self._run_flag:
                    self.cap.open(self.stream_url, cv2.CAP_FFMPEG)
                continue

            ret, cv_img = self.cap.read()
            if ret and cv_img is not None:
                self.change_pixmap_signal.emit(cv_img)
            else:
                self.show_waiting_screen()
                self.msleep(500)

        if self.cap:
            self.cap.release()

    def show_waiting_screen(self):
        blank = np.zeros((480, 640, 3), dtype=np.uint8)
        cv2.putText(blank, "Waiting for ESP32-CAM...", (80, 240),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.9, (200, 200, 200), 2)
        self.change_pixmap_signal.emit(blank)

    def stop(self):
        self._run_flag = False
        # 캡처 객체를 강제로 닫아 cap.read() 블로킹을 즉시 해제
        if self.cap and self.cap.isOpened():
            self.cap.release()
        self.wait(1000) # 최대 1초만 대기 후 강제 진행
        if self.isRunning():
            self.terminate() # 1초 내 미종료 시 강제 중단