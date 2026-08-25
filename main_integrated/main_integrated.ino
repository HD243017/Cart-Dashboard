#include <Wire.h>
#include <SoftwareSerial.h>

// ==================================================
// 블루투스 (HC-06)
// ==================================================
SoftwareSerial btSerial(2, 3);
SoftwareSerial espSerial(8, 12);

// UNO RX(D2) = HC-06 TX
// UNO TX(D3) = HC-06 RX

// ==================================================
// [IMU MPU6050 상수]
// ==================================================
const int MPU_ADDR = 0x68;
const float LIMIT_SHOCK_G = 2.5;             // 충격 판정 기준 2.5g
const float LIMIT_SLOPE_DEG = 40.0;          // 경사로 판정 기준 20도
const unsigned long PACKET_INTERVAL_MS = 50; // PC 데이터 전송 주기 (50ms)

// ==========================================
// [IMU 변수]
// ==========================================
int16_t raw_ax, raw_ay, raw_az;                           // 가속도
int16_t raw_gx, raw_gy, raw_gz;                           // 각속도

float offset_gx = 0.0, offset_gy = 0.0, offset_gz = 0.0;  // 계산된 변수
float pitch_angle = 0.0;                                  // 앞기울기
float roll_angle = 0.0;                                   // 옆기울기
float yaw_angle = 0.0;                                    // 회전값
float total_g = 1.0;                                      // 충격 벡터
String cart_status = "NORMAL";                            // 차량 상태

bool mpu_available = false; // IMU 연결 상태 확인용 변수 추가
float target_yaw = 0.0;     // 직진 보정용 목표 Yaw 각도 추가

unsigned long prev_time = 0;                              // 상보필터 계산에 필요한 이전 루프가 실행된 시간 
unsigned long last_send_time = 0;                         // 마지막 패킷 전송 시간 (비동기식 제어)

// ==================================================
// 차량 제어 관련 변수 및 상수
// ==================================================
const float YAW_KP = 8.5;             // 5.0 -> 8.5 (오차 대응 반응속도 향상)
const float YAW_DEADBAND = 0.5;       // 1.0 -> 0.5 (미세한 틀어짐부터 빠른 개입)
const int MAX_YAW_CORRECTION = 90;   // 60 -> 90 (모터 출력차 상한 확대)

// ==================================================
// 모터 핀
// ==================================================
const int ENA = 11; const int IN1 = 10; const int IN2 = 9;
const int ENB = 6;  const int IN3 = 5;  const int IN4 = 4;

// ==================================================
// 초음파 센서
// ==================================================
const int TRIG_PIN = A2;
const int ECHO_PIN = A3;
const int LIMIT_OBSTACLE_CM = 15;
int distance = -1;
unsigned long last_ultrasonic_time = 0;
const unsigned long ULTRASONIC_INTERVAL = 50;
bool obstacle_stop = false;

// ==================================================
// 속도 설정
// ==================================================
// 처음 출발할 때 속도
const int START_SPEED = 150;
const int MIN_SPEED = 80;
const int MAX_SPEED = 255;
const int ACCEL_STEP = 20;
const int DECEL_STEP = 10;
const unsigned long DECEL_INTERVAL = 50;
int speed = 0;

// ==================================================
// 차량 상태
// ==================================================
enum Motion { STOP, FORWARD, BACKWARD, LEFT, RIGHT };
Motion current_motion = STOP;
int driving_state = 0;                                  // 중요: 버튼 할당하고 배송 시작과 끝 상태 변수 수정 필요

// ==================================================
// 방향 전환 상태
// ==================================================
// 현재 방향을 유지하면서 자동 감속 중인지
bool changing_direction = false;
Motion next_motion = STOP;
unsigned long last_decel_time = 0;

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

  if (abs(gz_rate) < 0.5) gz_rate = 0.0;
  yaw_angle += gz_rate * dt;

  // Yaw 각도 360도 보정
  if (yaw_angle > 180.0) yaw_angle -= 360.0;
  if (yaw_angle < -180.0) yaw_angle += 360.0;

  // 가속도 센서를 이용한 정적 각도 계산
  float accel_pitch = atan2(ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
  float accel_roll = atan2(ay, sqrt(ax * ax + az * az)) * 180.0 / PI;

  // 상보 필터
  pitch_angle = 0.96 * (pitch_angle - gy_rate * dt) + 0.04 * accel_pitch;      // 0.96 * (이전각도 + 자이로변화량) + 0.04 * (가속도계 각도)
  roll_angle = 0.96 * (roll_angle + gx_rate * dt) + 0.04 * accel_roll;
  total_g = sqrt(ax * ax + ay * ay + az * az);                                // 충격량
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
  String packet = "car," +
                  String(yaw_angle, 1) + "," + 
                  String(pitch_angle, 1) + "," + 
                  String(roll_angle, 1) + "," + 
                  String(total_g, 2) + "," + 
                  cart_status + "," +
                  String(distance) + "," +
                  String(driving_state);

  // ESP-01로 단순 전송 (끝에 줄바꿈 \n 포함)
  espSerial.println(packet);
}

// ==================================================
// 초음파 거리 측정 (타임아웃 단축 적용)
// ==================================================
int get_distance()
{
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  // 10000us (약 1.7m) 타임아웃으로 지연 현상 방지
  long duration = pulseIn(ECHO_PIN, HIGH, 10000);
  if (duration == 0) return -1;
  return duration * 0.034 / 2;
}

// ==================================================
// 직진 방향 보정
// ==================================================
void correct_forward_direction()
{
  float yaw_error = target_yaw - yaw_angle;
  if (yaw_error > 180.0) yaw_error -= 360.0;
  if (yaw_error < -180.0) yaw_error += 360.0;

  if (abs(yaw_error) < YAW_DEADBAND) {
    forward();
    return;
  }

  int correction = (int)(YAW_KP * yaw_error);
  correction = constrain(correction, -MAX_YAW_CORRECTION, MAX_YAW_CORRECTION);

  int left_speed = constrain(speed + correction, MIN_SPEED, MAX_SPEED);
  int right_speed = constrain(speed - correction, MIN_SPEED, MAX_SPEED);

  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  analogWrite(ENA, left_speed); analogWrite(ENB, right_speed);
}

// ==================================================
// 기본 모터 동작 함수
// ==================================================
void forward() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  analogWrite(ENA, speed); analogWrite(ENB, speed);
}
void backward() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  analogWrite(ENA, speed); analogWrite(ENB, speed);
}
void left() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  analogWrite(ENA, speed); analogWrite(ENB, speed);
}
void right() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  analogWrite(ENA, speed); analogWrite(ENB, speed);
}
void stop_motor() {
  analogWrite(ENA, 0); analogWrite(ENB, 0);
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  current_motion = STOP; speed = 0; changing_direction = false; next_motion = STOP;
}
void stop_motor_without_reset() {
  analogWrite(ENA, 0); analogWrite(ENB, 0);
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
}

// ==================================================
// 방향 전환 시작
// ==================================================
void start_direction_change(Motion target_motion)
{
  changing_direction = true;
  next_motion = target_motion;
  last_decel_time = millis();
}

// ==================================================
// 방향 전환 자동 감속
// ==================================================
void update_direction_change()
{
  if (!changing_direction) return;
  unsigned long current_time = millis();
  if (current_time - last_decel_time < DECEL_INTERVAL) return;
  last_decel_time = current_time;
  speed -= DECEL_STEP;

  if (speed > 0) {
    if (current_motion == FORWARD) forward();
    else if (current_motion == BACKWARD) backward();
    return;
  }
  speed = 0;
  stop_motor_without_reset();
  current_motion = next_motion;
  changing_direction = false;
  speed = START_SPEED;

  if (current_motion == FORWARD) {
    if (obstacle_stop) { stop_motor(); return; }
    if (mpu_available) target_yaw = yaw_angle;
    forward();
  } else if (current_motion == BACKWARD) {
    backward();
  }
  next_motion = STOP;
}

// ==================================================
// F0 처리
// ==================================================
void handle_forward_command() {
  if (current_motion == FORWARD) {
    changing_direction = false;
    speed = min(speed + ACCEL_STEP, MAX_SPEED);
    if (mpu_available) correct_forward_direction();
    else forward();
    return;
  }
  if (current_motion == BACKWARD) {
    start_direction_change(FORWARD);
    return;
  }
  current_motion = FORWARD;
  changing_direction = false;
  speed = START_SPEED;
  if (mpu_available) target_yaw = yaw_angle;
  forward();
}

void handle_backward_command() {
  if (current_motion == BACKWARD) {
    changing_direction = false;
    speed = min(speed + ACCEL_STEP, MAX_SPEED);
    backward(); return;
  }
  if (current_motion == FORWARD){
    start_direction_change(BACKWARD);
    return;
  }
  current_motion = BACKWARD;
  changing_direction = false;
  speed = START_SPEED;
  backward();
}

void handle_left_command() {
  changing_direction = false;
  current_motion = LEFT;
  speed = START_SPEED;
  left();
}
void handle_right_command() {
  changing_direction = false;
  current_motion = RIGHT;
  speed = START_SPEED;
  right();
}

// ==================================================
// setup
// ==================================================
void setup()
{
  Serial.begin(115200);
  espSerial.begin(19200); // ESP-01 펌웨어 속도에 맞춰 9600 또는 115200 설정
  btSerial.begin(9600);
  btSerial.listen();      // 한번에 하나의 시리얼만 읽기에 BT 수신용

  // 핀 초기화
  pinMode(ENA, OUTPUT); pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(ENB, OUTPUT); pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
  stop_motor();

  Serial.println("=================================");
  Serial.println("차량 시스템 시작");
  Serial.println("HC-06 : READY");
  Serial.println("=================================");


  Serial.println("MPU6050 통신 시도 중...");
  Wire.begin();
  Wire.beginTransmission(MPU_ADDR);

  byte error = Wire.endTransmission();
  Serial.print("통신 결과 코드: ");
  Serial.println(error);

  if (Wire.endTransmission() == 0) {
    mpu_available = true;
    Serial.println("MPU6050 : 연결됨, 캘리브레이션 시작...");
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x6B); Wire.write(0x00); Wire.endTransmission();
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x1C); Wire.write(0x08); Wire.endTransmission();
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x1B); Wire.write(0x08); Wire.endTransmission();

    calibrate_imu();
    prev_time = millis();
    Serial.println("MPU6050 : 준비 완료");
  } else {
    Serial.println("MPU6050 : 연결 안됨");
  }

  Serial.println("ESP-01 부팅 대기 (12초)...");
  delay(12000);
  espSerial.println("CONFIG,3F_302,0424719222!!,192.168.0.164");
  delay(1000);

  Serial.println("차량 READY");
}

// ==================================================
// loop
// ==================================================
void loop()
{
  unsigned long current_time = millis();

  // MPU6050 업데이트
  if (mpu_available) {
    read_and_filter_imu(); // yaw_angle 지속 갱신
    check_safety_status();
  }

  if (current_time - last_send_time >= PACKET_INTERVAL_MS) {
    send_cart_packet();
    last_send_time = current_time;
  }

  // 초음파 측정
  if (current_time - last_ultrasonic_time >= ULTRASONIC_INTERVAL) {
    last_ultrasonic_time = current_time;
    distance = get_distance();
    obstacle_stop = (distance > 0 && distance < LIMIT_OBSTACLE_CM);

    if (current_motion == FORWARD && obstacle_stop) {
      Serial.println("!!! 장애물 감지 → 즉시 정지 !!!");
      stop_motor();
    }
  }

  // 블루투스 명령
  if (btSerial.available()) {
    String cmd = btSerial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "F" || cmd == "f" || cmd == "F0") {
      if (obstacle_stop) stop_motor(); else handle_forward_command();
    }
    else if (cmd == "B" || cmd == "b" || cmd == "B0") handle_backward_command();
    else if (cmd == "L" || cmd == "l" || cmd == "L0") handle_left_command();
    else if (cmd == "R" || cmd == "r" || cmd == "R0") handle_right_command();
    else if (cmd == "X" || cmd == "x" || cmd == "X0") stop_motor();
  }

  // 전진 ↔ 후진 자동 감속 처리
  update_direction_change();

  // MPU6050 직진 보정
  if (mpu_available && current_motion == FORWARD && speed > 0 && !changing_direction && !obstacle_stop)
  {
    correct_forward_direction();
  }
}