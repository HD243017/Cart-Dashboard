# camera_manager.py
from PyQt5.QtCore import QObject, pyqtSignal
from src.communication.videothread_capture import VideoThread
from src.gui import video_overlay

class CameraManager(QObject):
    # UI로 보낼 시그널: (완성된 QPixmap, 카운트 딕셔너리)
    frame_processed = pyqtSignal(object, dict)

    def __init__(self, url="http://192.168.0.83", parent=None):
        super().__init__(parent)
        self.url = url
        self.thread = VideoThread(self.url)
        self.thread.data_received_signal.connect(self._handle_raw_frame)

    def start(self):
        self.thread.start()

    def stop(self):
        if self.thread.isRunning():
            self.thread.stop()
            self.thread.wait()

    def _handle_raw_frame(self, cv_img, counts, detections=None):
        # 1. OpenCV 박스 그리기
        processed_img = video_overlay.draw_detections(cv_img, detections)
        # 2. QPixmap 변환
        pixmap = video_overlay.cv_to_pixmap(processed_img)
        # 3. UI 및 비즈니스 로직으로 전송
        self.frame_processed.emit(pixmap, counts)
