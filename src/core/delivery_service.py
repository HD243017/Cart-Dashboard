import threading
from typing import Dict, Optional

class DeliveryService:
    def __init__(self, db_manager, debounce_threshold: int = 3):
        self.db = db_manager
        self.current_order_id: Optional[int] = None
        self.prev_button_state: str = '0'
        
        # 현재 확정된 수량 및 배송 시작 수량
        self.current_counts: Dict[str, int] = {'r1': 0, 'g1': 0, 'y1': 0}
        self.start_counts: Dict[str, int] = {'r1': 0, 'g1': 0, 'y1': 0}

        # 연속 감지(디바운스)를 위한 상태 변수
        self.debounce_threshold = debounce_threshold  # 목표 연속 횟수 (3회)
        self.candidate_counts: Optional[Dict[str, int]] = None  # 새로 감지되어 관찰 중인 수량
        self.consecutive_count: int = 0  # 동일 수량 연속 감지 횟수

    def process_button_state(self, current_state: str):
        """UDP 패킷으로부터 수신된 버튼 상태(0/1) 처리"""
        # [0 -> 1] 배송 시작
        if current_state == '1' and self.prev_button_state == '0':
            self.start_counts = self.current_counts.copy()
            # 새로운 배송 시작 시 디바운스 버퍼 초기화
            self.candidate_counts = None
            self.consecutive_count = 0

            print(f"[SYSTEM] 배송 시작 - 초기 수량: {self.start_counts}")
            
            if self.db:
                self.current_order_id = self.db.start_new_order(
                    self.start_counts.get('r1', 0),
                    self.start_counts.get('g1', 0),
                    self.start_counts.get('y1', 0)
                )
                print(f"[SYSTEM] 신규 Order ID 발급: {self.current_order_id}")

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
                print(f"[SYSTEM] 주문 완료 처리 (ID: {self.current_order_id}, 상태: {final_status})")
                self.current_order_id = None
                self.candidate_counts = None
                self.consecutive_count = 0

        self.prev_button_state = current_state

    def update_vision_counts(self, counts: Dict[str, int]):
        """카메라에서 탐지된 최신 개수를 반영하고, 3회 연속 동일 수량 유지 시 DB order_logs에 기록"""
        # 배송 운행 중(상태 1)이고 order_id가 존재할 때
        if self.prev_button_state == '1' and self.current_order_id is not None:
            # 현재 확정된 수량과 들어온 수량이 다른지 확인
            is_different = any(
                counts.get(k, 0) != self.current_counts.get(k, 0)
                for k in ('r1', 'g1', 'y1')
            )

            if is_different:
                # 이전에 관찰 중이던 후보(candidate) 수량과 같은지 확인
                if counts == self.candidate_counts:
                    self.consecutive_count += 1
                else:
                    # 새로운 다른 수량이 들어오면 후보를 교체하고 카운트 리셋 (1회차)
                    self.candidate_counts = counts.copy()
                    self.consecutive_count = 1

                # 3회 연속 동일 수량으로 확인된 경우 -> 확정 및 DB 저장
                if self.consecutive_count >= self.debounce_threshold:
                    print(f"[SYSTEM] 수량 변동 확정 ({self.debounce_threshold}회 연속 감지) -> DB 기록: {counts}")
                    self.current_counts = counts.copy()
                    self.candidate_counts = None
                    self.consecutive_count = 0

                    if self.db:
                        threading.Thread(
                            target=self.db.insert_order_log,
                            args=(
                                self.current_order_id,
                                self.current_counts.get('r1', 0),
                                self.current_counts.get('g1', 0),
                                self.current_counts.get('y1', 0)
                            ),
                            daemon=True
                        ).start()
            else:
                # 기존 확정 수량과 같다면 후보 관찰 상태 리셋
                self.candidate_counts = None
                self.consecutive_count = 0

        else:
            # 배송 운행 중이 아닐 때는 대기 상태이므로 실시간 수량을 바로 동기화
            self.current_counts = counts.copy()
            self.candidate_counts = None
            self.consecutive_count = 0
