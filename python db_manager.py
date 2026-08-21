import pymysql

try:
    conn = pymysql.connect(
        host="localhost",
        user="root",
        password="azsx1234",       # 본인 MySQL 비밀번호[cite: 9]
        database="cart_db",  # 앞서 만든 DB 이름
        port=3306,
        charset="utf8mb4"
    )
    print("✅ MySQL 로컬 연결 성공!")
    conn.close()
except pymysql.MySQLError as e:
    print(f"❌ 연결 실패: {e}")