import asyncio
import json
import socket
import threading
import time
import requests
import numpy as np
import cv2
from fastapi import FastAPI, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse, StreamingResponse
from fastapi.templating import Jinja2Templates
from fastapi.encoders import jsonable_encoder

from src.database.db_manager import DBManager
from src.core.alert_filter import AlertFilter
from src.core.delivery_service import DeliveryService
from src.gui import video_overlay

app = FastAPI(title="Smart Cart Web Monitor")
templates = Jinja2Templates(directory="templates")

# ================== 전역 상태 ==================
cart_state = {
    "yaw": 0.0,
    "pitch": 0.0,
    "roll": 0.0,
    "g_val": 1.0,
    "speed": 0.0,
    "distance": 0,
    "status": "NORMAL",
    "led_counts": {"r1": 0, "g1": 0, "y1": 0}
}
latest_jpeg_bytes = None
frame_lock = threading.Lock()
connected_websockets = set()

# ================== DB 및 비즈니스 로직 ==================
try:
    db = DBManager()
    # 실제 커넥션 핑 테스트
    test_conn = db._get_connection()
    test_conn.close()
    db_connected = True
    print("[SYSTEM] 로컬 DB (MySQL: cartdb) 연결 성공")
except Exception as e:
    print(f"[SYSTEM] DB 연결 실패 (UI 단독 모드): {e}")
    db = None
    db_connected = False

delivery_svc = DeliveryService(db if db_connected else None)
alert_filter = AlertFilter()

# ================== 1. UDP 텔레메트리 수신 워커 ==================
def udp_worker():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", 5000))
    print("[UDP] 0.0.0.0:5000 리스닝 시작...")

    while True:
        try:
            data, _ = sock.recvfrom(1024)
            raw_data = data.decode('utf-8', errors='ignore').strip()
            if not raw_data:
                continue

            parts = raw_data.split(',')
            if parts[0].lower() == 'car' and len(parts) >= 8:
                yaw = float(parts[1])
                roll = -float(parts[2])
                pitch = -float(parts[3])
                g_val = float(parts[4])
                status = parts[5].strip()
                distance = int(parts[6].strip())
                button_state = parts[-1].strip()

                delivery_svc.process_button_state(button_state)

                cart_state.update({
                    "yaw": yaw,
                    "pitch": pitch,
                    "roll": roll,
                    "g_val": g_val,
                    "distance": distance,
                    "status": status
                })

                if db_connected:
                    alert_event = alert_filter.evaluate_imu(pitch, roll, g_val, distance)
                    if alert_event:
                        threading.Thread(
                            target=db.insert_driving_alert,
                            args=(alert_event, pitch, roll, g_val, distance),
                            daemon=True
                        ).start()
        except Exception:
            time.sleep(0.01)

# ================== 2. ESP32-CAM 이미지 & FOMO 폴링 워커 ==================
def camera_worker(base_url="http://192.168.0.83", interval=0.5):
    global latest_jpeg_bytes
    base_url = base_url.rstrip('/')
    print(f"[CAMERA] {base_url} 폴링 시작...")

    with requests.Session() as session:
        while True:
            loop_start = time.time()
            try:
                # 1. 캡처 이미지 요청
                img_url = f"{base_url}/image?t={int(time.time() * 1000)}"
                img_resp = session.get(img_url, timeout=2.0)

                # 2. FOMO 감지 데이터 요청
                data_url = f"{base_url}/data"
                data_resp = session.get(data_url, timeout=2.0)

                if img_resp.status_code == 200 and data_resp.status_code == 200:
                    img_array = np.frombuffer(img_resp.content, dtype=np.uint8)
                    cv_img = cv2.imdecode(img_array, cv2.IMREAD_COLOR)

                    # 객체 카운팅 및 필터링 (Score >= 0.75)
                    detections = data_resp.json()
                    counts = {'r1': 0, 'g1': 0, 'y1': 0}
                    valid_detections = []

                    for item in detections:
                        label = item.get('label')
                        score = item.get('value', 0.0)
                        if score >= 0.75 and label in counts:
                            valid_detections.append(item)
                            counts[label] += 1

                    # 상태 업데이트
                    cart_state["led_counts"] = counts
                    delivery_svc.update_vision_counts(counts)

                    # 바운딩 박스 오버레이
                    if cv_img is not None:
                        processed_img = video_overlay.draw_detections(cv_img, valid_detections)
                        ret, buffer = cv2.imencode('.jpg', processed_img, [cv2.IMWRITE_JPEG_QUALITY, 80])
                        if ret:
                            with frame_lock:
                                latest_jpeg_bytes = buffer.tobytes()

            except Exception:
                pass

            elapsed = time.time() - loop_start
            sleep_time = max(0.05, interval - elapsed)
            time.sleep(sleep_time)

threading.Thread(target=udp_worker, daemon=True).start()
threading.Thread(target=camera_worker, daemon=True).start()

# ================== 3. 라우트 및 WebSocket ==================
@app.get("/", response_class=HTMLResponse)
async def index(request: Request):
    return templates.TemplateResponse(request=request, name="index.html")

def generate_video_stream():
    global latest_jpeg_bytes
    while True:
        with frame_lock:
            frame = latest_jpeg_bytes
        if frame is None:
            time.sleep(0.05)
            continue

        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' + frame + b'\r\n')
        time.sleep(0.05)

@app.get("/video_feed")
async def video_feed():
    return StreamingResponse(
        generate_video_stream(),
        media_type="multipart/x-mixed-replace; boundary=frame"
    )

@app.websocket("/ws/telemetry")
async def websocket_telemetry(websocket: WebSocket):
    await websocket.accept()
    connected_websockets.add(websocket)
    try:
        while True:
            await websocket.send_text(json.dumps(cart_state))
            await asyncio.sleep(0.033)  # 30Hz
    except WebSocketDisconnect:
        connected_websockets.remove(websocket)

# ================== 4. DB 로그 조회 API ==================
@app.get("/api/db/logs")
async def get_db_logs():
    """PyQt LogViewerDialog와 동일하게 3개 테이블의 최근 기록 반환"""
    if not db_connected or db is None:
        return {
            "status": "error",
            "message": "로컬 DB 미연결 상태입니다.",
            "data": {"alerts": [], "orders": [], "order_logs": []}
        }
    try:
        alert_logs = db.fetch_recent_alerts(limit=100)
        order_logs = db.fetch_recent_orders(limit=50)
        order_detail_logs = db.fetch_recent_order_logs(limit=100)

        payload = {
            "alerts": alert_logs or [],
            "orders": order_logs or [],
            "order_logs": order_detail_logs or []
        }
        
        # datetime, decimal, None 등 MySQL 특수 객체를 일괄 JSON 직렬화 변환
        return {
            "status": "success",
            "data": jsonable_encoder(payload)
        }
    except Exception as e:
        print(f"[API ERROR] /api/db/logs 조회 중 오류: {e}")
        return {
            "status": "error",
            "message": f"DB 조회 실패: {str(e)}",
            "data": {"alerts": [], "orders": [], "order_logs": []}
        }
