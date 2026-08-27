import time

class AlertFilter:
    def __init__(self):
        # 상태 플래그
        self.tilt_alert_active = False
        self.obstacle_alert_active = False
        
        # 충격 쿨다운 관리
        self.last_shock_time = 0.0
        self.SHOCK_COOLDOWN_SEC = 2.0  # 충격 발생 후 2초간 중복 기록 차단

        # 임계값 상수
        self.TILT_ENTER_DEG = 25.0     # 경사 진입 기준
        self.TILT_EXIT_DEG = 22.0      # 경사 해제 기준
        self.SHOCK_LIMIT_G = 2.5       # 충격 기준
        self.OBSTACLE_ENTER_CM = 15    # 장애물 진입 기준
        self.OBSTACLE_EXIT_CM = 20     # 장애물 해제 기준

    def evaluate_imu(self, pitch, roll, g_val, distance=None):
        # DB에 기록해야 할 새 경고가 있으면 warning_type 반환,없으면 None 반환
        current_time = time.time()
        max_tilt = max(abs(pitch), abs(roll))

        # 충격 감지 최우선 순위 (쿨다운)
        if g_val >= self.SHOCK_LIMIT_G:
            if current_time - self.last_shock_time > self.SHOCK_COOLDOWN_SEC:
                self.last_shock_time = current_time
                return "SHOCK"

        if distance > 0: # 0 이하나 -1(에러)은 제외
            if not self.obstacle_alert_active:
                if distance <= self.OBSTACLE_ENTER_CM:
                    self.obstacle_alert_active = True
                    return "OBSTACLE"
            else:
                # 20cm 이상 멀어지면 경고 상태 해제
                if distance >= self.OBSTACLE_EXIT_CM:
                    self.obstacle_alert_active = False

        # 경사로 감지 (히스테리시스)
        if not self.tilt_alert_active:
            if max_tilt >= self.TILT_ENTER_DEG:
                self.tilt_alert_active = True
                return "TILT_DANGER"
        else:
            if max_tilt <= self.TILT_EXIT_DEG:
                self.tilt_alert_active = False  # 안전 구역 진입 시 리셋

        return None