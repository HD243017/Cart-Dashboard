import sys
from PyQt5.QtWidgets import QApplication
from dashboard import DashboardWindow

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = DashboardWindow()
    window.show()
    sys.exit(app.exec_())