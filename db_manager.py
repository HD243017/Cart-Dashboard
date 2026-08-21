import pymysql

DB_CONFIG = {
    "host":"localhost",
    "user": "root",
    "password":"azsx1234",
    "database":"cart_db",
    "port": 3306,
    "charset": "utf8mb4"
}

class DBManager:
    def __init__(self):
        self.config = DB_CONFIG

    def _get_connection(self):
        # DB 커넥션 객체 생성
        return pymysql.connect(**self.config)

    def insert_driving_alert(self, warning_type, pitch, roll, g_force, ultrasonic_distance=None):
        sql = """
        INSERT INTO driving_alerts (warning_type, pitch, roll, g_force, ultrasonic_distance)
        VALUES (%s, %s, %s, %s, %s)
        """
        try:
            with self._get_connection() as conn:
                with conn.cursor() as cursor:
                    cursor.execute(sql, (warning_type, pitch, roll, g_force, ultrasonic_distance))

                    conn.commit()

        except Exception as e:
            print(f"[DB ERROR] 기록 실패: {e}")

    def fetch_recent_alerts(self, limit=100):
        # 최근 기록된 N개의 경고 로그 조회
        sql = """
        SELECT log_id, warning_type, pitch, roll, g_force, ultrasonic_distance, created_at
        FROM driving_alerts
        ORDER BY log_id DESC
        LIMIT %s
        """
        try:
            with self._get_connection() as conn:
                # cursor() 대신 딕셔너리 커서를 사용하도록 지정
                with conn.cursor(pymysql.cursors.DictCursor) as cursor:
                    cursor.execute(sql, (limit,))
                    return cursor.fetchall()
        except Exception as e:
            print(f"[DB ERROR] 조회 실패: {e}")
            return []
        
    def start_new_order(self, start_red, start_green, start_yellow):
        # 배송 시작 (0->1) 초기 상태 인서트 후 order_id 반환
        sql = """
        INSERT INTO orders (start_red, start_green, start_yellow, status)
        VALUES (%s, %s, %s, 'IN_PROGRESS')
        """
        try:
            with self._get_connection() as conn:
                with conn.cursor() as cursor:
                    cursor.execute(sql, (start_red, start_green, start_yellow))
                    conn.commit()
                    return cursor.lastrowid # 방금 생성된 order_id를 반환 (중요!)
        except Exception as e:
            print(f"[DB ERROR] 주문 시작 기록 실패: {e}")
            return None

    def update_order_end(self, order_id, end_red, end_green, end_yellow, status):
        # 배송 종료 (1->0) 누락 여부를 판단 최종 개수 업데이트
        sql = """
        UPDATE orders 
        SET end_red=%s, end_green=%s, end_yellow=%s, status=%s, end_time=CURRENT_TIMESTAMP
        WHERE order_id=%s
        """
        try:
            with self._get_connection() as conn:
                with conn.cursor() as cursor:
                    cursor.execute(sql, (end_red, end_green, end_yellow, status, order_id))
                    conn.commit()
        except Exception as e:
            print(f"[DB ERROR] 주문 종료 업데이트 실패: {e}")

    def insert_order_log(self, order_id, red_count, green_count, yellow_count):
        # 배송 중 변화 감지 시 기록
        sql = """
        INSERT INTO order_logs (order_id, red_count, green_count, yellow_count)
        VALUES (%s, %s, %s, %s)
        """
        try:
            with self._get_connection() as conn:
                with conn.cursor() as cursor:
                    cursor.execute(sql, (order_id, red_count, green_count, yellow_count))
                    conn.commit()
        except Exception as e:
            print(f"[DB ERROR] 주문 로그 기록 실패: {e}")

    def fetch_recent_orders(self, limit=50):
        # ui에 띄울 orders 테이블
        sql = "SELECT * FROM orders ORDER BY order_id DESC LIMIT %s"
        try:
            with self._get_connection() as conn:
                with conn.cursor(pymysql.cursors.DictCursor) as cursor:
                    cursor.execute(sql, (limit,))
                    return cursor.fetchall()
        except Exception as e:
            return []

    def fetch_recent_order_logs(self, limit=100):
        # ui에 띄울 order_logs 테이블
        sql = "SELECT * FROM order_logs ORDER BY log_id DESC LIMIT %s"
        try:
            with self._get_connection() as conn:
                with conn.cursor(pymysql.cursors.DictCursor) as cursor:
                    cursor.execute(sql, (limit,))
                    return cursor.fetchall()
        except Exception as e:
            return []