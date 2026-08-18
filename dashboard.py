import cv2
from PyQt5.QtWidgets import QWidget, QMessageBox, QVBoxLayout
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QImage, QPixmap
from PyQt5 import uic
import pyqtgraph as pg
from videothread import VideoThread

# ==========================================
# 2. 메인 관제 GUI 윈도우
# ==========================================
class DashboardWindow(QWidget):
    def __init__(self):
        super().__init__()
        
        # UI 파일 로드 (반드시 같은 경로에 dashboard.ui가 있어야 함)
        uic.loadUi("dashboard.ui", self)
        self.video_label.setScaledContents(False)
        # 1. 그래프 위젯 초기화 세팅
        self.init_graph()
        
        # 2. 버튼 이벤트 연결
        self.btn_db_log.clicked.connect(self.show_db_popup)
        
        # 3. 영상 스레드 시작
        self.start_video_stream()

        # ==========================================
        # ★ 추가: 아두이노 시리얼 통신 연결
        # ==========================================
        # self.serial_port = 'COM4' # 본인의 아두이노 포트(예: COM4, COM5)로 꼭 변경!
        # self.baudrate = 115200
        # try:
        #     self.serial = serial.Serial(self.serial_port, self.baudrate, timeout=0.1)
        #     print(f"{self.serial_port} 포트 연결 성공!")
        # except Exception as e:
        #     print(f"시리얼 연결 실패! 아두이노 포트를 확인해 줘: {e}")
        #     self.serial = None
        
        # # 4. 50ms마다 실제 패킷을 확인하는 타이머 시작
        # self.timer = QTimer(self)
        # self.timer.timeout.connect(self.receive_and_parse_packet) 
        # self.timer.start(50)

    def init_graph(self):
        # UI 파일에 비워둔 graph_widget 안에 pyqtgraph를 채워 넣는 작업
        self.graph_layout = QVBoxLayout(self.graph_widget)
        self.graph_layout.setContentsMargins(0, 0, 0, 0) # 여백 제거
        
        self.plot_widget = pg.PlotWidget()
        self.plot_widget.setBackground('#1e1e2f')
        self.plot_widget.showGrid(x=True, y=True, alpha=0.3)
        self.plot_widget.setYRange(-30, 30) # 기울기 Y축 범위 고정 (-30도 ~ 30도)
        
        self.curve = self.plot_widget.plot(pen=pg.mkPen(color='#a6e3a1', width=2))
        self.graph_layout.addWidget(self.plot_widget)
        
        # 초기 그래프 데이터 버퍼 세팅
        self.graph_data = [0] * 100 

    def show_db_popup(self):
        # 멘토의 팁: DB 기록을 보여주는 창은 무겁지 않게 별도 다이얼로그로 띄우는게 좋아!
        QMessageBox.information(self, "DB 기록 시스템", "여기에 로컬 MySQL과 연동된 물류/에러 로그 열람창이 뜰 예정이야!")

    # ----------------------------------------------------
    # 센서 데이터 파싱 및 UI 업데이트 (멘토링 핵심 로직)
    # ----------------------------------------------------
    def update_imu_data(self, yaw, pitch, roll, g_val, status):
        """
        IMU 패킷 파싱 후 관련 UI만 갱신하는 함수
        (초음파 거리나 모터 속도는 다른 패킷에서 처리하도록 분리)
        """
        # 빵보드 방향 고려: roll = 앞뒤 기울기, pitch = 옆기울기
        self.lbl_yaw.setText(f"• 회전값(Yaw) : {yaw:.1f} °")
        self.lbl_tilt.setText(f"• 앞뒤 기울기 : {roll:.1f} °")
        self.lbl_tilt_side.setText(f"• 옆기울기 : {pitch:.1f} °")
        self.lbl_g.setText(f"• 가속도(G) : {g_val:.2f} G")
        
        # 상태 표시 업데이트
        if status == "NORMAL":
            self.lbl_status.setText("현재 상태 : NORMAL (정상)")
            self.lbl_status.setStyleSheet("background-color: #a6e3a1; color: #11111b; font-weight: bold; border-radius: 6px; padding: 6px;")
        else:
            self.lbl_status.setText(f"경고 : 🚨 {status}")
            self.lbl_status.setStyleSheet("background-color: #f38ba8; color: #11111b; font-weight: bold; border-radius: 6px; padding: 6px;")

        # 실시간 그래프 업데이트 (카트의 핵심인 앞뒤 기울기 'roll'을 그래프에 표시)
        self.graph_data = self.graph_data[1:] + [roll]
        self.curve.setData(self.graph_data)

    def receive_and_parse_packet(self):
        """
        통신 모듈로부터 데이터를 받아오는 부분 (지연 현상 완벽 해결)
        """
        if hasattr(self, 'serial') and self.serial and self.serial.is_open:
            try:
                latest_line = None
                
                # 💡 핵심 해결책: 버퍼에 데이터가 남아있는 동안 계속 읽어서 '최신 데이터'만 남기기
                while self.serial.in_waiting > 0:
                    # errors='ignore'를 추가해서 통신 노이즈로 인한 찌꺼기 텍스트 에러 방지
                    latest_line = self.serial.readline().decode('utf-8', errors='ignore').strip()
                
                # 밀린 데이터를 다 비우고, 가장 최신 데이터가 존재할 때만 파싱 시작!
                if latest_line:
                    parts = latest_line.split(',')
                    
                    if len(parts) >= 6 and parts[0] == "IMU":
                        yaw = float(parts[1])
                        pitch = float(parts[2])  # 옆기울기
                        roll = float(parts[3])   # 앞뒤 기울기
                        g_val = float(parts[4])
                        status = parts[5]
                        
                        self.update_imu_data(yaw, pitch, roll, g_val, status)
                        
            except Exception as e:
                # 파싱 중 발생하는 자잘한 에러는 무시 (프로그램 다운 방지)
                pass

    # ----------------------------------------------------
    # 영상 프레임 업데이트 슬롯
    # ----------------------------------------------------
    def start_video_stream(self):
        esp32_cam_url = "http://192.168.0.83:81/" 
        
        self.thread = VideoThread(esp32_cam_url)
        self.thread.change_pixmap_signal.connect(self.update_image)
        self.thread.start()

    def update_image(self, cv_img):
        rgb_image = cv2.cvtColor(cv_img, cv2.COLOR_BGR2RGB)
        h, w, ch = rgb_image.shape
        bytes_per_line = ch * w
        qt_img = QImage(rgb_image.data, w, h, bytes_per_line, QImage.Format_RGB888)
        
        target_w = self.video_label.width() if self.video_label.width() > 0 else 640
        target_h = self.video_label.height() if self.video_label.height() > 0 else 480
        pixmap = QPixmap.fromImage(qt_img).scaled(target_w, target_h, Qt.KeepAspectRatio)
        self.video_label.setPixmap(pixmap)

    def closeEvent(self, event):
        # 윈도우 닫힐 때 안전하게 스레드 종료
        if hasattr(self, 'thread') and self.thread.isRunning():
            self.thread.stop()
        event.accept()