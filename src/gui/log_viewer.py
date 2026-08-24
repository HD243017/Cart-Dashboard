from PyQt5.QtWidgets import QDialog, QVBoxLayout, QTabWidget, QTableWidget, QTableWidgetItem, QHeaderView
from PyQt5.QtCore import Qt

class LogViewerDialog(QDialog):
    def __init__(self, alert_logs=None, order_logs=None, order_detail_logs=None, parent=None):
        super().__init__(parent)
        self.setWindowTitle("통합 시스템 로그 뷰어")
        self.resize(850, 450)

        # 테마 UI 스타일시트
        self.setStyleSheet("""
            /* 다이얼로그 전체 배경 */
            QDialog {
                background-color: #1e1e2f;
            }
            
            /* 탭 배경 및 테두리 */
            QTabWidget::pane {
                border: 1px solid #45475a;
                background-color: #1e1e2f;
            }
            
            /* 기본 탭 버튼 스타일 (선택 안 된 상태) */
            QTabBar::tab {
                background-color: #313244;
                color: #cdd6f4;
                padding: 8px 20px;
                margin-right: 2px;
                border-top-left-radius: 4px;
                border-top-right-radius: 4px;
            }
            
            /* 선택된 탭 버튼 스타일 (강조) */
            QTabBar::tab:selected {
                background-color: #1e1e2f;
                color: #a6e3a1;
                font-weight: bold;
                border-top: 2px solid #a6e3a1;
            }
            
            /* 테이블 전체 배경 및 글자색 */
            QTableWidget {
                background-color: #1e1e2f;
                color: #cdd6f4;
                gridline-color: #313244;
                border: none;
            }
            
            /* 테이블 상단 헤더 및 좌측 번호 헤더 */
            QHeaderView::section {
                background-color: #313244;
                color: #cdd6f4;
                padding: 5px;
                border: 1px solid #1e1e2f;
                font-weight: bold;
            }
            
            /* 좌측 상단 모서리 빈 공간 */
            QTableCornerButton::section {
                background-color: #313244;
                border: 1px solid #1e1e2f;
            }
        """)

        # DB 연결 실패나 에러로 데이터가 안 넘어왔을 때 프로그램이 죽지 않도록 빈 리스트 할당
        alert_logs = alert_logs or []
        order_logs = order_logs or []
        order_detail_logs = order_detail_logs or []

        layout = QVBoxLayout(self)
        
        # 탭 위젯 생성
        self.tabs = QTabWidget()
        layout.addWidget(self.tabs)

        # 3개의 테이블 위젯 생성
        self.table_alerts = QTableWidget()
        self.table_orders = QTableWidget()
        self.table_order_details = QTableWidget()

        # 탭에 추가
        self.tabs.addTab(self.table_alerts, "이상상황 로그")
        self.tabs.addTab(self.table_orders, "배송 내역")
        self.tabs.addTab(self.table_order_details, "배송중 로그")

        # 테이블 세팅 및 데이터 삽입
        self.setup_alerts_table()
        self.populate_alerts(alert_logs)

        self.setup_orders_table()
        self.populate_orders(order_logs)

        self.setup_order_details_table()
        self.populate_order_details(order_detail_logs)

    def _set_table_common(self, table, headers):
        # 테이블 공통 설정 (중복 코드 최소화)
        table.setColumnCount(len(headers))
        table.setHorizontalHeaderLabels(headers)
        header = table.horizontalHeader()
        header.setSectionResizeMode(QHeaderView.Stretch)
        header.setSectionResizeMode(0, QHeaderView.ResizeToContents)
        header.setStretchLastSection(False)
        table.setEditTriggers(QTableWidget.NoEditTriggers) # 읽기 전용

    def setup_alerts_table(self):
        headers = ["ID", "경고 종류", "피치(°)", "롤(°)", "가속도(G)", "거리(cm)", "발생 시각"]
        self._set_table_common(self.table_alerts, headers)

    def setup_orders_table(self):
        headers = ["주문 ID", "시작 (R/G/Y)", "종료 (R/G/Y)", "상태", "시작 시간", "종료 시간"]
        self._set_table_common(self.table_orders, headers)

    def setup_order_details_table(self):
        headers = ["로그 ID", "주문 ID", "빨강", "초록", "노랑", "기록 시간"]
        self._set_table_common(self.table_order_details, headers)

    def populate_alerts(self, logs):
        self.table_alerts.setRowCount(len(logs))
        for row_idx, log in enumerate(logs):
            items = [
                str(log.get("log_id", "")),
                str(log.get("warning_type", "")),
                f"{log.get('pitch', 0.0):.1f}" if log.get("pitch") is not None else "-",
                f"{log.get('roll', 0.0):.1f}" if log.get("roll") is not None else "-",
                f"{log.get('g_force', 0.0):.2f}" if log.get("g_force") is not None else "-",
                str(log.get("ultrasonic_distance")) if log.get("ultrasonic_distance") is not None else "-",
                str(log.get("created_at", ""))
            ]
            self._add_row_items(self.table_alerts, row_idx, items)

    def populate_orders(self, logs):
        self.table_orders.setRowCount(len(logs))
        for row_idx, log in enumerate(logs):
            # get()의 기본값을 0으로 주어 키가 없어도 안전하게 문자열 완성
            start_counts = f"{log.get('start_red', 0)} / {log.get('start_green', 0)} / {log.get('start_yellow', 0)}"
            
            # 종료 상태는 아직 배송 중일 때 NULL일 수 있음
            if log.get('end_red') is not None:
                end_counts = f"{log.get('end_red')} / {log.get('end_green')} / {log.get('end_yellow')}"
            else:
                end_counts = "- / - / -"

            items = [
                str(log.get("order_id", "")),
                start_counts,
                end_counts,
                str(log.get("status", "UNKNOWN")),
                str(log.get("start_time", "")),
                str(log.get("end_time", "")) if log.get("end_time") else "-"
            ]
            self._add_row_items(self.table_orders, row_idx, items)

    def populate_order_details(self, logs):
        self.table_order_details.setRowCount(len(logs))
        for row_idx, log in enumerate(logs):
            items = [
                str(log.get("log_id", "")),
                str(log.get("order_id", "")),
                str(log.get("red_count", 0)),
                str(log.get("green_count", 0)),
                str(log.get("yellow_count", 0)),
                str(log.get("created_at", ""))
            ]
            self._add_row_items(self.table_order_details, row_idx, items)

    def _add_row_items(self, table, row_idx, items):
        # 테이블에 아이템을 채우는 공통 함수
        for col_idx, text in enumerate(items):
            item = QTableWidgetItem(text)
            item.setTextAlignment(Qt.AlignCenter)
            table.setItem(row_idx, col_idx, item)