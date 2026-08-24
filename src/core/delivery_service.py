import threading
from typing import Dict, Optional

class DeliveryService:
    def __init__(self, db_manager):
        self.db = db_manager
        self.current_order_id: Optional[int] = None
        self.prev_button_state: str = '0'
        self.current_counts: Dict[str, int] = {'r1': 0, 'g1': 0, 'y1': 0}
        self.start_counts: Dict[str, int] = {'r1': 0, 'g1': 0, 'y1': 0}

    def process_button_state(self, current_state: str):
        """IMU 또는 센서 패킷에서 수신한 버튼 상태(0/1) 처리"""
        # [0 -> 1] 배송 시작
        if current_state == '1' and self.prev_button_state == '0':
            self.start_counts = self.current_counts.copy()
            print(f"[SYSTEM] 배송 시작 - 초기 수량: {self.start_counts}")
            
            if self.db:
                self.current_order_id = self.db.start_new_order(
                    self.start_counts.get('r1', 0),
                    self.start_counts.get('g1', 0),
                    self.start_counts.get('y1', 0)
                )

        # [1 -> 0] 배송 종료
        elif current_state == '0' and self.prev_button_state == '1':
            print("[SYSTEM] 배송 종료 감지")
            if self.db and self.current_order_id is not None:
                end_counts = self.current_counts
                is_match = (
                    end_counts.get('r1', 0) == self.start_counts.get('r1', 0) and
                    end_counts.get('g1', 0) == self.start_counts.get('g1', 0) and
                    end_counts.get('y1', 0) == self.start_counts.get('y1', 0)
                )
                final_status = 'COMPLETED' if is_match else 'MISSING'

                self.db.update_order_end(
                    self.current_order_id,
                    end_counts.get('r1', 0),
                    end_counts.get('g1', 0),
                    end_counts.get('y1', 0),
                    final_status
                )
                self.current_order_id = None

        self.prev_button_state = current_state

    def update_vision_counts(self, counts: Dict[str, int]):
        """카메라에서 탐지된 최신 개수를 반영하고 운행 중 변동 시 order_logs에 기록"""
        # 배송 운행 중(상태 1)이고 order_id가 존재할 때 수량 변화 검사
        if self.prev_button_state == '1' and self.current_order_id is not None:
            changed = any(
                counts.get(k, 0) != self.current_counts.get(k, 0)
                for k in ('r1', 'g1', 'y1')
            )
            if changed:
                print(f"[SYSTEM] 배송 중 수량 변동 감지: {counts}")
                if self.db:
                    threading.Thread(
                        target=self.db.insert_order_log,
                        args=(
                            self.current_order_id,
                            counts.get('r1', 0),
                            counts.get('g1', 0),
                            counts.get('y1', 0)
                        ),
                        daemon=True
                    ).start()

        self.current_counts = counts.copy()
