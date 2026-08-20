import socket
import time
from PyQt5.QtCore import QThread, pyqtSignal

class UDPThread(QThread):
    # UI로 데이터를 쏴줄 시그널 정의
    packet_received = pyqtSignal(str)

    def __init__(self, ip="0.0.0.0", port=5000):
        super().__init__()
        self.ip = ip
        self.port = port
        self.running = True
        self.sock = None

        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.bind((self.ip, self.port))
            self.sock.setblocking(False) # 비동기 모드
            print(f"UDP 포트 {self.port}에서 수신 대기 중...")
        except Exception as e:
            print(f"UDP 소켓 생성 실패: {e}")
            self.sock = None

    def run(self):
        # 백그라운드에서 데이터 수신
        while self.running:
            if self.sock:
                latest_line = None
                try:
                    # 버퍼에 쌓인 데이터 중 가장 최신 데이터만 획득
                    while True:
                        data, addr = self.sock.recvfrom(1024)
                        latest_line = data.decode('utf-8', errors='ignore').strip()
                except BlockingIOError:
                    pass 

                if latest_line:
                    self.packet_received.emit(latest_line)
            
            # CPU 과부하 방지 대기
            time.sleep(0.01)

    def stop(self):
        self.running = False
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass