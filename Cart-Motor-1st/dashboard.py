import cv2
from PyQt5.QtWidgets import QWidget, QMessageBox, QVBoxLayout
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QImage, QPixmap
from PyQt5 import uic
import pyqtgraph as pg

from videothread import VideoThread
from udp_comm import UDPThread

# =========
# 메인 GUI
# =========
class DashboardWindow(QWidget):
    def __init__(self):
        super().__init__()
        
        # UI 파일 로드 같은 경로에 dashboard.ui O
        uic.loadUi("dashboard.ui", self)
        self.video_label.setScaledContents(False)

        
        self.init_graph() # 그래프 위젯 초기화 세팅
        self.btn_db_log.clicked.connect(self.show_db_popup) # 버튼 이벤트 연결
        self.start_video_stream() # 영상 스레드 시작

        # UDP 스레드 연결 및 실행
        self.udp_thread = UDPThread(ip="0.0.0.0", port=5000)
        self.udp_thread.packet_received.connect(self.route_packet) # 통신 스레드에서 데이터 수신시 UI업데이트 함수 실행하도록 연결
        self.udp_thread.start()

    def init_graph(self):
        self.graph_layout = QVBoxLayout(self.graph_widget) # UI 파일에 비워둔 graph_widget 안에 pyqtgraph를 채워 넣는 작업
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
        QMessageBox.information(self, "DB 기록 시스템", "여기에 로컬 MySQL과 연동된 물류/에러 로그 열람창이 뜰 예정!")


    def route_packet(self, raw_data):
        # 패킷 예시: [헤더,데이터,데이터,...,데이터,상태]
        parts = raw_data.split(',')
        if not parts:
            return

        header = parts[0].lower() # 소문자로 변환

        # 헤더 분기
        if header == 'imu' and len(parts) >= 6:
            try:
                yaw = float(parts[1])
                pitch = float(parts[2])
                roll = float(parts[3])
                g_val = float(parts[4])
                status = parts[5].strip()

                self.update_imu_data(yaw, pitch, roll, g_val, status)
            except ValueError:
                pass

        # 이 장소에 elif 문으로 각각의 헤더를 추가하시기 바랍니다.
        # 추가될 영상 및 이미지는 TCP방식이 맞다고 보여지기에 따로 해주시기 바랍니다. 

    # ===========
    # UI 업데이트
    # ===========
    def update_imu_data(self, yaw, pitch, roll, g_val, status):
        self.lbl_yaw.setText(f"• 회전값(Yaw) : {yaw:.1f} °")
        self.lbl_tilt.setText(f"• 앞뒤 기울기 : {roll:.1f} °")
        self.lbl_tilt_side.setText(f"• 옆기울기 : {pitch:.1f} °")
        self.lbl_g.setText(f"• 가속도(G) : {g_val:.2f} G")
        
        if status == "NORMAL":
            self.lbl_status.setText("현재 상태 : NORMAL (정상)")
            self.lbl_status.setStyleSheet("background-color: #a6e3a1; color: #11111b; font-weight: bold; border-radius: 6px; padding: 6px;")
        else:
            self.lbl_status.setText(f"경고 : 🚨 {status}")
            self.lbl_status.setStyleSheet("background-color: #f38ba8; color: #11111b; font-weight: bold; border-radius: 6px; padding: 6px;")

        # 실시간 그래프 업데이트 (메모(JYJ): roll 이외에 옆으로 기우는 상황 고려하여 다시 코드 작성 필요)
        self.graph_data = self.graph_data[1:] + [roll]
        self.curve.setData(self.graph_data)

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
            self.thread.wait()
        if hasattr(self, 'udp_thread') and self.udp_thread.isRunning():
            self.udp_thread.stop()
            self.udp_thread.wait()
        event.accept()