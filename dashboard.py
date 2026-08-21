import cv2
import threading
from PyQt5.QtWidgets import QWidget, QMessageBox, QVBoxLayout
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QImage, QPixmap
from PyQt5 import uic
import pyqtgraph as pg
from videothread_capture import VideoThread

from udp_comm import UDPThread
from db_manager import DBManager
from alert_filter import AlertFilter
from log_viewer import LogViewerDialog

# =========
# 메인 GUI
# =========
class DashboardWindow(QWidget):
    def __init__(self):
        super().__init__()
        
        # UI 파일 로드 같은 경로에 dashboard.ui O
        uic.loadUi("dashboard.ui", self)
        self.video_label.setScaledContents(False)

        try:
            self.db = DBManager()  # 본인 비밀번호 확인
            self.db_connected = True
            self.prev_button_state = '0'
            self.current_order_id = None
            self.current_counts = {'r1': 0, 'g1': 0, 'y1': 0}
            self.start_counts = {'r1': 0, 'g1': 0, 'y1': 0}

        except Exception as e:
            print(f"[SYSTEM] 로컬 DB 연결 실패. 기록 기능 없이 UI만 실행됩니다: {e}")
            self.db_connected = False
        self.alert_filter = AlertFilter()

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

        self.plot_widget.addLegend(offset=(10, 10))
        
        self.curve_pitch = self.plot_widget.plot(pen=pg.mkPen(color='#a6e3a1', width=2), name="앞뒤 기울기 (Pitch)")
        self.curve_roll = self.plot_widget.plot(pen=pg.mkPen(color='#89b4fa', width=2), name="옆기울기 (Roll)")
        self.graph_layout.addWidget(self.plot_widget)
        
        # 초기 그래프 데이터 버퍼 세팅
        self.graph_data_pitch = [0] * 100 
        self.graph_data_roll = [0] * 100

    def show_db_popup(self):
        # 탭에 들어갈 데이터 빈 리스트로 초기화
        alert_logs = []
        order_logs = []
        order_detail_logs = []

        # DB가 연결되어 있을 때만 데이터를 조회 시도
        if getattr(self, 'db_connected', False):
            alert_logs = self.db.fetch_recent_alerts(limit=100)
            order_logs = self.db.fetch_recent_orders(limit=50)
            order_detail_logs = self.db.fetch_recent_order_logs(limit=100)
        else:
            # DB가 연결 안 된 상태라면
            QMessageBox.information(self, "알림", "DB 미연결 상태입니다. 빈 화면으로 뷰어를 엽니다.")

        # 3개의 인자를 모두 넣어 다이얼로그 실행
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
        if header == 'imu' and len(parts) >= 6:
            try:
                yaw = float(parts[1])
                pitch = float(parts[2])
                roll = float(parts[3])
                g_val = float(parts[4])
                status = parts[5].strip()
                button_state = parts[-1].strip()

                self.process_delivery_state(button_state)

                self.update_imu_data(yaw, pitch, roll, g_val, status)
            except ValueError:
                pass

        # 이 장소에 elif 문으로 각각의 헤더를 추가하시기 바랍니다.
        # 추가될 영상 및 이미지는 TCP방식이 맞다고 보여지기에 따로 해주시기 바랍니다.


    def process_delivery_state(self, current_state):
        # [0 -> 1] 배송 시작 감지
        if current_state == '1' and self.prev_button_state == '0':
            print("[SYSTEM] 배송 시작")
            # 시작 개수 고정
            self.start_counts = self.current_counts.copy() 
            
            if getattr(self, 'db_connected', False):
                # DB 기록, 발급된 order_id를 변수에 저장
                self.current_order_id = self.db.start_new_order(
                    self.start_counts['r1'], 
                    self.start_counts['g1'], 
                    self.start_counts['y1']
                )

        # [1 -> 0] 배송 종료 감지
        elif current_state == '0' and self.prev_button_state == '1':
            print("[SYSTEM] 배송이 종료되었습니다.")
            if getattr(self, 'db_connected', False) and self.current_order_id is not None:
                # 최종 현재 개수
                end_counts = self.current_counts
                
                # 누락 여부 판별 로직 (시작 개수와 종료 개수 비교)
                if (end_counts['r1'] == self.start_counts['r1'] and
                    end_counts['g1'] == self.start_counts['g1'] and
                    end_counts['y1'] == self.start_counts['y1']):
                    final_status = 'COMPLETED'  # 정상 완료
                else:
                    final_status = 'MISSING'    # 누락 발생
                    
                # DB에 종료 기록 업데이트
                self.db.update_order_end(
                    self.current_order_id,
                    end_counts['r1'], end_counts['g1'], end_counts['y1'],
                    final_status
                )
                self.current_order_id = None # 다음 배송을 위해 초기화

        # 현재 상태를 이전 상태로 업데이트 (0->0, 1->1 연속 신호 시엔 위 조건문을 타지 않고 이 줄만 실행됨)
        self.prev_button_state = current_state 

    # ===========
    # UI 업데이트
    # ===========
    def update_imu_data(self, yaw, pitch, roll, g_val, status):
        self.lbl_yaw.setText(f"• 회전값(Yaw) : {yaw:.1f} °")
        self.lbl_tilt.setText(f"• 앞뒤 기울기 : {pitch:.1f} °")
        self.lbl_tilt_side.setText(f"• 옆기울기 : {roll:.1f} °")
        self.lbl_g.setText(f"• 가속도(G) : {g_val:.2f} G")

        if getattr(self, 'prev_status', "") != status:
            if status == "NORMAL":
                self.lbl_status.setText("현재 상태 : NORMAL (정상)")
                self.lbl_status.setStyleSheet("background-color: #a6e3a1; color: #11111b; font-weight: bold; border-radius: 6px; padding: 6px;")
            else:
                self.lbl_status.setText(f"경고 : 🚨 {status}")
                self.lbl_status.setStyleSheet("background-color: #f38ba8; color: #11111b; font-weight: bold; border-radius: 6px; padding: 6px;")


        if getattr(self, 'db_connected', False):
            try:
                alert_event = self.alert_filter.evaluate_imu(pitch, roll, g_val)
                if alert_event: 
                    #데몬 스레드로 비동기 처리
                    threading.Thread(
                        target=self.db.insert_driving_alert,
                        args=(alert_event, pitch, roll, g_val, None),
                        daemon=True
                    ).start()

            except Exception as e:
                print(f"[DB 저장 에러]: {e}")


        # 실시간 그래프 업데이트 (앞뒤 기울기와 옆기울기 동시 렌더링)
        self.graph_data_pitch = self.graph_data_pitch[1:] + [pitch]
        self.graph_data_roll = self.graph_data_roll[1:] + [roll]
        
        self.curve_pitch.setData(self.graph_data_pitch)
        self.curve_roll.setData(self.graph_data_roll)

    # ----------------------------------------------------
    # 영상 프레임 업데이트 슬롯
    # ----------------------------------------------------
    def start_video_stream(self):
        esp32_cam_url = "http://192.168.0.83" 
        
        self.thread = VideoThread(esp32_cam_url)
        self.thread.data_received_signal.connect(self.update_camera_and_counts)
        self.thread.start()

    def update_camera_and_counts(self, cv_img, counts):
        rgb_image = cv2.cvtColor(cv_img, cv2.COLOR_BGR2RGB)
        h, w, ch = rgb_image.shape
        bytes_per_line = ch*w
        qt_img = QImage(rgb_image.data, w, h, bytes_per_line, QImage.Format_RGB888)

        target_w = self.video_label.width() if self.video_label.width() > 0 else 640
        target_h = self.video_label.height() if self.video_label.height() > 0 else 480
        pixmap = QPixmap.fromImage(qt_img).scaled(target_w, target_h, Qt.KeepAspectRatio)
        self.video_label.setPixmap(pixmap)

        if hasattr(self, 'lbl_red'):
            self.lbl_red.setText(f"RED LED : {counts['r1']}개")
        if hasattr(self, 'lbl_green'):
            self.lbl_green.setText(f"GREEN LED : {counts['g1']}개")
        if hasattr(self, 'lbl_yellow'):
            self.lbl_yellow.setText(f"YELLOW LED : {counts['y1']}개")

        # 배송 중(상태 1)일 때만 물건 개수 변화 감지
        if hasattr(self, 'prev_button_state') and self.prev_button_state == '1':
            # 이전 카운트와 현재 카메라 카운트 다른경우
            if (counts['r1'] != self.current_counts.get('r1', 0) or
                counts['g1'] != self.current_counts.get('g1', 0) or
                counts['y1'] != self.current_counts.get('y1', 0)):
                
                print(f"[SYSTEM] 수량 변화 {counts}")
                # DB에 변화된 로그 기록 (비동기 처리 영상 끊김 방지)
                if getattr(self, 'db_connected', False) and getattr(self, 'current_order_id', None):
                    threading.Thread(
                        target=self.db.insert_order_log,
                        args=(self.current_order_id, counts['r1'], counts['g1'], counts['y1']),
                        daemon=True
                    ).start()
        
        # 항상 최신 카운트 개수 업데이트 - 배송 중이 아닐 때도 현재 개수는 파악하고 있어야 시작할 때 기록 가능
        if hasattr(self, 'current_counts'):
            self.current_counts = counts.copy()


    def closeEvent(self, event):
        # 윈도우 닫힐 때 안전하게 스레드 종료
        if hasattr(self, 'thread') and self.thread.isRunning():
            self.thread.stop()
            self.thread.wait()
        if hasattr(self, 'udp_thread') and self.udp_thread.isRunning():
            self.udp_thread.stop()
            self.udp_thread.wait()
        event.accept()
