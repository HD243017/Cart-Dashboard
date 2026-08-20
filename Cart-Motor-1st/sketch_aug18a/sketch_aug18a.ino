#include <Wire.h>

// ==========================================
// [IMU 관련 상수값]
// ==========================================
const int MPU_ADDR = 0x68;
const float LIMIT_SHOCK_G = 2.5;             // 충격 판정 기준 2.5g
const float LIMIT_SLOPE_DEG = 20.0;          // 경사로 판정 기준 20도
const unsigned long PACKET_INTERVAL_MS = 50; // PC 데이터 전송 주기 (50ms)

// ==========================================
// [IMU에서 사용할 변수값]
// ==========================================
int16_t raw_ax, raw_ay, raw_az;
int16_t raw_gx, raw_gy, raw_gz;

float offset_gx = 0.0, offset_gy = 0.0, offset_gz = 0.0;       // 캘리 영점 오프셋 값, X, Y 자이로 오프셋 추가
float pitch_angle = 0.0;     // 앞뒤 경사각 (Pitch)
float roll_angle = 0.0;      // 좌우 경사각 (Roll) - 새로 추가됨!
float yaw_angle = 0.0;       // Z축 회전 각도 (Yaw)
float total_g = 1.0;         // 3축 합성 가속도 크기
String cart_status = "NORMAL"; // 현재상태

unsigned long prev_time = 0;
unsigned long last_send_time = 0; // PC 패킷 전송 시간 기록용

// ==========================================
// [IMU 관련 함수들]
// ==========================================

// 1. 평평한 바닥 기준 상태의 기준점 기록
void calibrate_imu() {
  long sum_gx = 0, sum_gy = 0, sum_gz = 0;
  for (int i = 0; i < 500; i++) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x43); // 자이로 데이터 시작 레지스터 (0x43 ~ 0x48)
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR, 6, true);
    
    sum_gx += (Wire.read() << 8 | Wire.read());
    sum_gy += (Wire.read() << 8 | Wire.read());
    sum_gz += (Wire.read() << 8 | Wire.read());
    delay(3);
  }
  offset_gx = (float)sum_gx / 500.0;
  offset_gy = (float)sum_gy / 500.0;
  offset_gz = (float)sum_gz / 500.0;
}

// 2. 원시 데이터를 읽어 상보필터 및 물리단위 적용
void read_and_filter_imu() {
  unsigned long current_time = millis();
  float dt = (current_time - prev_time) / 1000.0;
  prev_time = current_time;

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 14, true);

  raw_ax = (Wire.read() << 8 | Wire.read());
  raw_ay = (Wire.read() << 8 | Wire.read());
  raw_az = (Wire.read() << 8 | Wire.read());
  Wire.read(); Wire.read(); // 온도 데이터 무시
  raw_gx = (Wire.read() << 8 | Wire.read());
  raw_gy = (Wire.read() << 8 | Wire.read());
  raw_gz = (Wire.read() << 8 | Wire.read());

  // 가속도 물리 단위 환산
  float ax = (float)raw_ax / 8192.0;
  float ay = (float)raw_ay / 8192.0;
  float az = (float)raw_az / 8192.0;

  // 자이로 각속도 계산 (오프셋 적용)
  float gx_rate = ((float)raw_gx - offset_gx) / 65.5;
  float gy_rate = ((float)raw_gy - offset_gy) / 65.5;
  float gz_rate = ((float)raw_gz - offset_gz) / 65.5;

  // Yaw 데드밴드 필터
  if (abs(gz_rate) < 1.2) gz_rate = 0.0;
  yaw_angle += gz_rate * dt;

  // 가속도 센서만을 이용한 거친 기울기 값
  float accel_pitch = atan2(ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
  float accel_roll = atan2(ay, sqrt(ax * ax + az * az)) * 180.0 / PI;

  // 💡 [핵심] 상보필터 적용: 자이로(단기적 정확함) 96% + 가속도(장기적 기준점) 4%
  // 급가속 시 가속도 센서가 튀는 현상을 자이로가 꽉 잡아줌!
  pitch_angle = 0.96 * (pitch_angle + gy_rate * dt) + 0.04 * accel_pitch;
  roll_angle = 0.96 * (roll_angle + gx_rate * dt) + 0.04 * accel_roll;
  
  // 3축 합성 가속도(충격량) 산출
  total_g = sqrt(ax * ax + ay * ay + az * az);
}

// 3. 상태 판별 로직 (1순위 충격, 2순위 경사로)
void check_safety_status() {
  if (total_g >= LIMIT_SHOCK_G) {
    cart_status = "SHOCK";
  // 앞뒤 혹은 좌우 기울기 중 하나라도 한계치를 넘으면 경고
  } else if (abs(pitch_angle) >= LIMIT_SLOPE_DEG || abs(roll_angle) >= LIMIT_SLOPE_DEG) {
    cart_status = "SLOPE";
  } else {
    cart_status = "NORMAL";
  }
}

// 4. 50ms 주기마다 PC로 표준 패킷 전송 (요청하신 순서 적용)
void send_cart_packet() {
  // 규격: Header, Yaw, Pitch, Roll, G, Status\n
  // 파싱 오류 방지를 위해 쉼표 뒤 공백 절대 금지
  Serial.print("IMU,");
  Serial.print(yaw_angle, 1);
  Serial.print(",");
  Serial.print(pitch_angle, 1);
  Serial.print(",");
  Serial.print(roll_angle, 1);
  Serial.print(",");
  Serial.print(total_g, 2);
  Serial.print(",");
  Serial.print(cart_status);
  Serial.print("\n");
}

// ==========================================
// [초기 설정 및 메인 루프]
// ==========================================
void setup() {
  Serial.begin(115200);
  Wire.begin();

  // MPU6050 슬립 해제
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission();

  // 가속도 ±4G, 자이로 ±500 deg/s 로 범위 설정
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  Wire.write(0x08);
  Wire.endTransmission();

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B);
  Wire.write(0x08);
  Wire.endTransmission();

  // 영점 캘리브레이션 (시작할 때 센서가 평평해야 함)
  calibrate_imu();

  prev_time = millis();
}

// 다른 센서 및 모터 구동 코드와 통합하기 쉽게 루프를 가장 아래로 분리
void loop() {
  unsigned long current_time = millis();
  
  // 50ms (초당 20회) 마다 한 번씩만 데이터를 읽고 전송 (센서 과부하 방지)
  if (current_time - last_send_time >= PACKET_INTERVAL_MS) {
    read_and_filter_imu();
    check_safety_status();
    send_cart_packet();
    
    last_send_time = current_time;
  }
  
  // 추후 이 곳에 초음파 센서나 모터 구동 코드를 추가하면 됩니다.
}