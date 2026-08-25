#include <Wire.h>
#include <SoftwareSerial.h>

// ESP-01과 통신할 핀 설정 (RX: 2, TX: 3)
SoftwareSerial espSerial(8, 12);

// ==========================================
// [IMU 관련 상수값]
// ==========================================
const int MPU_ADDR = 0x68;
const float LIMIT_SHOCK_G = 2.5;             // 충격 판정 기준 2.5g
const float LIMIT_SLOPE_DEG = 20.0;          // 경사로 판정 기준 20도
const unsigned long PACKET_INTERVAL_MS = 100; // PC 데이터 전송 주기 (50ms)

// ==========================================
// [변수]
// ==========================================

int16_t raw_ax, raw_ay, raw_az;
int16_t raw_gx, raw_gy, raw_gz;

float offset_gx = 0.0, offset_gy = 0.0, offset_gz = 0.0;       
float pitch_angle = 0.0;     
float roll_angle = 0.0;      
float yaw_angle = 0.0;       
float total_g = 1.0;         
String cart_status = "NORMAL"; 

unsigned long prev_time = 0;
unsigned long last_send_time = 0; 

// ==========================================
// [IMU 캘리브레이션 및 필터 함수]
// ==========================================
void calibrate_imu() {
  long sum_gx = 0, sum_gy = 0, sum_gz = 0;
  for (int i = 0; i < 500; i++) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x43); 
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR, 6, true);
    
    // I2C 통신 문제로 2번 쪼개진 통신을 16바이트로 합치기
    sum_gx += (Wire.read() << 8 | Wire.read());
    sum_gy += (Wire.read() << 8 | Wire.read());
    sum_gz += (Wire.read() << 8 | Wire.read());
    delay(3);
  }
  offset_gx = (float)sum_gx / 500.0;
  offset_gy = (float)sum_gy / 500.0;
  offset_gz = (float)sum_gz / 500.0;
}

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
  Wire.read(); Wire.read(); // 온도 무시
  raw_gx = (Wire.read() << 8 | Wire.read());
  raw_gy = (Wire.read() << 8 | Wire.read());
  raw_gz = (Wire.read() << 8 | Wire.read());

  float ax = (float)raw_ax / 8192.0;
  float ay = (float)raw_ay / 8192.0;
  float az = (float)raw_az / 8192.0;

  // 영점 오프셋을 빼고 각속도 변환
  float gx_rate = ((float)raw_gx - offset_gx) / 65.5;
  float gy_rate = ((float)raw_gy - offset_gy) / 65.5;
  float gz_rate = ((float)raw_gz - offset_gz) / 65.5;

  if (abs(gz_rate) < 1.2) gz_rate = 0.0;
  yaw_angle += gz_rate * dt;

  // 가속도 센서를 이용한 정적 각도 계산
  float accel_pitch = atan2(ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
  float accel_roll = atan2(ay, sqrt(ax * ax + az * az)) * 180.0 / PI;

  // 상보 필터
  // 0.96 * (이전각도 + 자이로변화량) + 0.04 * (가속도계 각도)
  pitch_angle = 0.96 * (pitch_angle - gy_rate * dt) + 0.04 * accel_pitch;
  roll_angle = 0.96 * (roll_angle + gx_rate * dt) + 0.04 * accel_roll;
  
  // 충격량
  total_g = sqrt(ax * ax + ay * ay + az * az);
}

void check_safety_status() {
  if (total_g >= LIMIT_SHOCK_G) {
    cart_status = "SHOCK";
  } else if (abs(pitch_angle) >= LIMIT_SLOPE_DEG || abs(roll_angle) >= LIMIT_SLOPE_DEG) {
    cart_status = "SLOPE";
  } else {
    cart_status = "NORMAL";
  }
}

// ==========================================
// [AT 명령어를 통한 UDP 패킷 전송 함수]
// ==========================================
void send_cart_packet() {
  // 5가지 데이터를 쉼표로 연결
  String packet = "imu," +
                  String(yaw_angle, 1) + "," + 
                  String(pitch_angle, 1) + "," + 
                  String(roll_angle, 1) + "," + 
                  String(total_g, 2) + "," + 
                  cart_status + ",0";

  // ESP-01로 단순 전송 (끝에 줄바꿈 \n 포함)
  espSerial.println(packet);
}

// ==========================================
// [초기 설정 및 메인 루프]
// ==========================================
void setup() {
  Serial.begin(115200);
  espSerial.begin(19200); // ESP-01 펌웨어 속도에 맞춰 9600 또는 115200 설정

  Serial.println("ESP-01 부팅 대기 (12초)...");
  delay(12000);

  espSerial.println("CONFIG,3F_302,0424719222!!,192.168.0.164");
  delay(1000);

  Wire.begin();
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission();

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  Wire.write(0x08);
  Wire.endTransmission();

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B);
  Wire.write(0x08);
  Wire.endTransmission();

  calibrate_imu();
  prev_time = millis();
}

void loop() {
  read_and_filter_imu();
  unsigned long current_time = millis();
  if (current_time - last_send_time >= PACKET_INTERVAL_MS) {
    send_cart_packet();
    check_safety_status();
    
    last_send_time = current_time;
  }
}