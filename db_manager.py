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