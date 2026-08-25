import math
import time
import threading
from PyQt5.QtWidgets import QWidget, QMessageBox, QVBoxLayout
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QImage, QPixmap
from PyQt5 import uic
import pyqtgraph as pg

from src.communication.udp_comm import UDPThread
from src.database.db_manager import DBManager
from src.core.alert_filter import AlertFilter
from src.gui.log_viewer import LogViewerDialog
from src.core.delivery_service import DeliveryService
from src.gui import video_overlay
from src.communication.camera_manager import CameraManager
from src.gui.cart_3d_viewer import Cart3DViewer
MODERN_STYLE = """
QWidget#Dashboard {
    background-color: #0f111a;
    color: #cdd6f4;
    font-family: "Pretendard", "Segoe UI", "Malgun Gothic", sans-serif;
}
QFrame#frame_video, QFrame#frame_telemetry_container, QFrame#frame_graph, QFrame#frame_3d {
    background-color: #181926;
    border: 1px solid #24273a;
    border-radius: 12px;
}
QLabel#video_label {
    background-color: #11121d;
    border-radius: 8px;
    color: #6e738d;
    font-size: 14px;
}
QLabel#lbl_ai_title, QLabel#lbl_sensor_title, QLabel#lbl_graph_title, QLabel#lbl_3d_title {
    color: #5cffd1;
    font-size: 14px;
    font-weight: bold;
    padding-bottom: 1px;
}
QLabel#lbl_red {
    background-color: #3b222e; color: #ed8796;
    border: 1px solid #5a2e3f; border-radius: 6px; padding: 5px 8px;
    font-weight: bold; font-size: 12px;
}
QLabel#lbl_green {
    background-color: #233b2e; color: #a6da95;
    border: 1px solid #325942; border-radius: 6px; padding: 5px 8px;
    font-weight: bold; font-size: 12px;
}
QLabel#lbl_yellow {
    background-color: #3d3725; color: #eed49f;
    border: 1px solid #5d5332; border-radius: 6px; padding: 5px 8px;
    font-weight: bold; font-size: 12px;
}
QLabel#lbl_status {
    background-color: #1e3a2f; color: #a6da95;
    border: 1px solid #2e5e47; font-weight: bold; font-size: 12px;
    border-radius: 6px; padding: 6px 10px;
}
QPushButton#btn_db_log {
    background-color: #181926; color: #a5adcb;
    font-weight: bold; font-size: 12px;
    border: 1px solid rgba(255, 255, 255, 0.15); border-radius: 6px; padding: 6px 12px;
}
QPushButton#btn_db_log:hover {
    background-color: #1e2030; color: #ffffff; border: 1px solid #ffffff;
}
QLabel#lbl_yaw, QLabel#lbl_speed, QLabel#lbl_tilt, 
QLabel#lbl_tilt_side, QLabel#lbl_dist, QLabel#lbl_g {
    background-color: #1e2030; color: #cad3f5;
    border: 1px solid #2b2f46; border-radius: 6px; padding: 6px 8px; font-size: 12px;
}
"""

class DashboardWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.latest_imu = {"yaw": 0.0, "pitch": 0.0, "roll": 0.0}

        uic.loadUi(r"src\gui\dashboard.ui", self)
        self.setStyleSheet(MODERN_STYLE)
        self.video_label.setScaledContents(False)

        try:
            self.db = DBManager()
            self.db_connected = True
        except Exception as e:
            print(f"[SYSTEM] 로컬 DB 연결 실패. 기록 기능 없이 UI만 실행됩니다: {e}")
            self.db = None
            self.db_connected = False

        self.delivery_svc = DeliveryService(self.db if self.db_connected else None)
        self.alert_filter = AlertFilter()

        self.init_graph()
        self.init_3d_viewer()

        self.btn_db_log.clicked.connect(self.show_db_popup)

        self.camera_mgr = CameraManager("http://192.168.0.83", parent=self)
        self.camera_mgr.frame_processed.connect(self.on_frame_received)
        self.camera_mgr.start()

        self.udp_thread = UDPThread(ip="0.0.0.0", port=5000)
        self.udp_thread.packet_received.connect(self.route_packet)
        self.udp_thread.start()

        self.timer_3d = QTimer(self)
        self.timer_3d.timeout.connect(self.sync_3d_viewer)
        self.timer_3d.start(33) # 30FPS로 3D 뷰어 업데이트

    def init_graph(self):
        graph_layout = QVBoxLayout(self.graph_widget)
        graph_layout.setContentsMargins(0, 0, 0, 0)

        self.plot_widget = pg.PlotWidget()
        self.plot_widget.setBackground('#11121d')
        self.plot_widget.showGrid(x=True, y=True, alpha=0.18)
        self.plot_widget.setYRange(-30, 30)

        axis_pen = pg.mkPen(color='#494d64', width=1)
        self.plot_widget.getAxis('left').setPen(axis_pen)
        self.plot_widget.getAxis('bottom').setPen(axis_pen)
        self.plot_widget.getAxis('left').setTextPen('#a5adcb')
        self.plot_widget.getAxis('bottom').setTextPen('#a5adcb')

        self.plot_widget.addLegend(offset=(10, 10), labelTextColor='#cad3f5')
        self.curve_pitch = self.plot_widget.plot(
            pen=pg.mkPen(color='#a6da95', width=2.2), name="Pitch (앞뒤)"
        )
        self.curve_roll = self.plot_widget.plot(
            pen=pg.mkPen(color='#8aadf4', width=2.2), name="Roll (좌우)"
        )
        graph_layout.addWidget(self.plot_widget)

        self.graph_data_pitch = [0] * 100
        self.graph_data_roll = [0] * 100

    def init_3d_viewer(self):
        v3d_layout = QVBoxLayout(self.widget_3d_container)
        v3d_layout.setContentsMargins(0, 0, 0, 0)

        self.cart_3d = Cart3DViewer()
        v3d_layout.addWidget(self.cart_3d)

    def show_db_popup(self):
        alert_logs = []
        order_logs = []
        order_detail_logs = []

        if getattr(self, 'db_connected', False):
            alert_logs = self.db.fetch_recent_alerts(limit=100)
            order_logs = self.db.fetch_recent_orders(limit=50)
            order_detail_logs = self.db.fetch_recent_order_logs(limit=100)
        else:
            QMessageBox.information(self, "알림", "DB 미연결 상태입니다. 빈 화면으로 뷰어를 엽니다.")

        dialog = LogViewerDialog(alert_logs, order_logs, order_detail_logs, self)
        dialog.exec_()

    def route_packet(self, raw_data):
        # 패킷 예시: [헤더,데이터,데이터,...,데이터,상태]
        if not raw_data:
            return
        parts = raw_data.split(',')
        if not parts:
            return

        header = parts[0].lower() # 소문자로 변환

        # 헤더 분기
        if header == 'car' and len(parts) >= 8:
            try:
                yaw = float(parts[1])
                roll = -float(parts[2])
                pitch = -float(parts[3])
                g_val = float(parts[4])
                status = parts[5].strip()
                distance = int(parts[6].strip())
                button_state = parts[-1].strip()

                self.delivery_svc.process_button_state(button_state)

                self.update_data(yaw, pitch, roll, g_val, distance, status)
            except ValueError:
                pass

    def update_data(self, yaw, pitch, roll, g_val, distance, status):
        self.lbl_yaw.setText(f"• 회전값(Yaw) : {yaw:.1f} °")
        self.lbl_tilt.setText(f"• 앞뒤 기울기 : {pitch:.1f} °")
        self.lbl_tilt_side.setText(f"• 옆기울기 : {roll:.1f} °")
        self.lbl_g.setText(f"• 가속도(G) : {g_val:.2f} G")
        self.lbl_dist.setText(f"• 초음파 거리 : {distance} cm")

        if getattr(self, 'prev_status', "") != status:
            self.prev_status = status
            if status == "NORMAL":
                self.lbl_status.setText("상태 : NORMAL (정상 주행)")
                self.lbl_status.setStyleSheet(
                    "background-color: #1e3a2f; color: #a6da95; border: 1px solid #2e5e47; "
                    "font-weight: bold; font-size: 12px; border-radius: 6px; padding: 6px 10px;"
                )
            else:
                self.lbl_status.setText(f"경고 : 🚨 {status}")
                self.lbl_status.setStyleSheet(
                    "background-color: #3e1f2b; color: #ed8796; border: 1px solid #6e2d3b; "
                    "font-weight: bold; font-size: 12px; border-radius: 6px; padding: 6px 10px;"
                )

        if self.db_connected:
            try:
                alert_event = self.alert_filter.evaluate_imu(pitch, roll, g_val, distance)
                if alert_event: 
                    #데몬 스레드로 비동기 처리
                    threading.Thread(
                        target=self.db.insert_driving_alert,
                        args=(alert_event, pitch, roll, g_val, distance),
                        daemon=True
                    ).start()

            except Exception as e:
                print(f"[DB 저장 에러]: {e}")

        # 2D 시계열 그래프 갱신
        self.graph_data_pitch = self.graph_data_pitch[1:] + [pitch]
        self.graph_data_roll = self.graph_data_roll[1:] + [roll]
        self.curve_pitch.setData(self.graph_data_pitch)
        self.curve_roll.setData(self.graph_data_roll)

        # 3D 카트 뷰어 실시간 동기화
        self.latest_imu["yaw"] = yaw
        self.latest_imu["pitch"] = pitch
        self.latest_imu["roll"] = roll

    def sync_3d_viewer(self):
        # 타이머 주기에 맞춰 한 번만 렌더링
        self.cart_3d.update_pose(
            self.latest_imu["yaw"], 
            self.latest_imu["pitch"], 
            self.latest_imu["roll"]
    )

    def on_frame_received(self, pixmap, counts):
        self.delivery_svc.update_vision_counts(counts)
        self.video_label.setPixmap(pixmap)

        if hasattr(self, 'lbl_red'):
            self.lbl_red.setText(f"RED LED : {counts.get('r1', 0)}개")
        if hasattr(self, 'lbl_green'):
            self.lbl_green.setText(f"GREEN LED : {counts.get('g1', 0)}개")
        if hasattr(self, 'lbl_yellow'):
            self.lbl_yellow.setText(f"YELLOW LED : {counts.get('y1', 0)}개")

    def closeEvent(self, event):
        if hasattr(self, 'camera_mgr'):
            self.camera_mgr.stop()
        if hasattr(self, 'udp_thread') and self.udp_thread.isRunning():
            self.udp_thread.stop()
            self.udp_thread.wait()
        event.accept()
