from PyQt5.QtWidgets import QDialog, QVBoxLayout, QTableWidget, QTableWidgetItem, QHeaderView
from PyQt5.QtCore import Qt

class LogViewerDialog(QDialog):
    def __init__(self, logs, parent=None):
        super().__init__(parent)
        self.setWindowTitle("최근 돌발상황 및 경고 로그")
        self.resize(750, 380)

        layout = QVBoxLayout(self)
        self.table = QTableWidget()
        layout.addWidget(self.table)

        self.setup_table()
        self.populate_data(logs)

    def setup_table(self):
        headers = ["ID", "경고 종류", "피치(°)", "롤(°)", "가속도(G)", "거리(cm)", "발생 시각"]
        self.table.setColumnCount(len(headers))
        self.table.setHorizontalHeaderLabels(headers)
        
        # 열 너비 자동 맞춤
        header = self.table.horizontalHeader()
        header.setSectionResizeMode(QHeaderView.Stretch)
        header.setSectionResizeMode(0, QHeaderView.ResizeToContents) # ID 열은 내용 크기 맞춤
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)       # 수정 불가 읽기 전용

    def populate_data(self, logs):
        self.table.setRowCount(len(logs))

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

            for col_idx, text in enumerate(items):
                item = QTableWidgetItem(text)
                item.setTextAlignment(Qt.AlignCenter)
                self.table.setItem(row_idx, col_idx, item)