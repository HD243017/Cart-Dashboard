import socket

# 수신할 포트 번호 (아두이노 설정과 일치해야 함)
UDP_PORT = 5000

# 소켓 생성 및 바인딩
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", UDP_PORT))

print(f"🚀 {UDP_PORT}번 포트에서 UDP 수신 대기 중... (종료: Ctrl+C)")

while True:
    try:
        # 데이터 수신 대기 (최대 1024 바이트)
        data, addr = sock.recvfrom(1024)
        
        # 바이트 데이터를 문자열로 변환
        received_text = data.decode('utf-8', errors='ignore').strip()
        
        print(f"[수신 성공] 보낸 기기 IP: {addr[0]} | 데이터: {received_text}")
    except KeyboardInterrupt:
        print("\n수신 테스트를 종료합니다.")
        break
    except Exception as e:
        print(f"에러 발생: {e}")