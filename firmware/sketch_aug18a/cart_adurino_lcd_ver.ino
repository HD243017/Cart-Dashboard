#include <Wire.h>
#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h>

// ==================================================
// 블루투스 (HC-06)
// ==================================================
SoftwareSerial btSerial(2, 3);
SoftwareSerial espSerial(8, 12);

// UNO RX(D2) = HC-06 TX
// UNO TX(D3) = HC-06 RX

// ==================================================
// [I2C LCD]
// ==================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);  // 주소, 열 16, 행 2 (모듈 사이즈에 맞게 수정)

String prev_cart_status = "";        // 이전 상태 저장용 (변경 시에만 갱신)
int prev_driving_state = -1;         // 이전 상태 저장용 (변경 시에만 갱신)

// ==================================================
// [IMU MPU6050 상수]
// ==================================================
const int MPU_ADDR = 0x68;
const float LIMIT_SHOCK_G = 2.5;             // 충격 판정 기준 2.5g
const float LIMIT_SLOPE_DEG = 25.0;          // 경사로 판정 기준 25도
const unsigned long PACKET_INTERVAL_MS = 100; // PC 데이터 전송 주기 (50ms)

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
const float YAW_KP = 12.0;            // 8.5 -> 12.0 (오차 반응 강화)
const float YAW_DEADBAND = 0.2;       // 0.5 -> 0.2 (더 좁은 오차부터 보정 시작)
const int MAX_YAW_CORRECTION = 110;   // 90 -> 110 (게인 상승분 커버)

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
const int START_SPEED = 120;
const int MIN_SPEED = 70;
const int MAX_SPEED = 200;
const int ACCEL_STEP = 10;
const int DECEL_STEP = 10;
const unsigned long DECEL_INTERVAL = 50;
int speed = 0;

// ==================================================
// 차량 상태
// ==================================================
enum Motion { STOP, FORWARD, BACKWARD, LEFT, RIGHT };
Motion current_motion = STOP;
int driving_state = 0;

// ==================================================
// 방향 전환 상태
// ==================================================
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
  Wire.read(); Wire.read();
  raw_gx = (Wire.read() << 8 | Wire.read());
  raw_gy = (Wire.read() << 8 | Wire.read());
  raw_gz = (Wire.read() << 8 | Wire.read());

  float ax = (float)raw_ax / 8192.0;
  float ay = (float)raw_ay / 8192.0;
  float az = (float)raw_az / 8192.0;

  float gx_rate = ((float)raw_gx - offset_gx) / 65.5;
  float gy_rate = ((float)raw_gy - offset_gy) / 65.5;
  float gz_rate = ((float)raw_gz - offset_gz) / 65.5;

  if (abs(gz_rate) < 0.5) gz_rate = 0.0;
  yaw_angle += gz_rate * dt;

  if (yaw_angle > 180.0) yaw_angle -= 360.0;
  if (yaw_angle < -180.0) yaw_angle += 360.0;

  float accel_pitch = atan2(ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
  float accel_roll = atan2(ay, sqrt(ax * ax + az * az)) * 180.0 / PI;

  pitch_angle = 0.96 * (pitch_angle - gy_rate * dt) + 0.04 * accel_pitch;
  roll_angle = 0.96 * (roll_angle + gx_rate * dt) + 0.04 * accel_roll;
  total_g = sqrt(ax * ax + ay * ay + az * az);
}

void check_safety_status() {
  if (total_g >= LIMIT_SHOCK_G) {
    cart_status = "SHOCK";
    stop_motor();
  } else if (abs(pitch_angle) >= LIMIT_SLOPE_DEG || abs(roll_angle) >= LIMIT_SLOPE_DEG) {
    cart_status = "SLOPE";
    stop_motor();
  } else if (distance > 0 && distance < LIMIT_OBSTACLE_CM) {
    cart_status = "OBSTACLE";
  } else {
    cart_status = "NORMAL";
  }
}

// ==========================================
// [LCD 1행 상태 메시지 출력 함수 - setup 단계용]
// ==========================================
void lcd_show_stage(String msg) {
  lcd.setCursor(0, 0);
  lcd.print("                ");  // 1행 지우기
  lcd.setCursor(0, 0);
  lcd.print(msg);
}

// ==========================================
// [LCD 상태 표시 함수 - 평소 운행 중]
// ==========================================
void update_lcd() {
  if (cart_status != prev_cart_status) {
    lcd.setCursor(0, 0);
    lcd.print("                ");
    lcd.setCursor(0, 0);
    lcd.print("Status: " + cart_status);
    prev_cart_status = cart_status;
  }

  if (driving_state != prev_driving_state) {
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print("Driving: " + String(driving_state));
    prev_driving_state = driving_state;
  }
}

// ==========================================
// [AT 명령어를 통한 UDP 패킷 전송 함수]
// ==========================================
void send_cart_packet() {
  String packet = "car," +
                  String(yaw_angle, 1) + "," + 
                  String(pitch_angle, 1) + "," + 
                  String(roll_angle, 1) + "," + 
                  String(total_g, 2) + "," + 
                  cart_status + "," +
                  String(distance) + "," +
                  String(driving_state);

  espSerial.println(packet);
  Serial.println(packet);
}

// ==================================================
// 초음파 거리 측정 (타임아웃 단축 적용)
// ==================================================
int get_distance()
{
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
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
  espSerial.begin(19200);
  btSerial.begin(9600);
  
  pinMode(ENA, OUTPUT); pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(ENB, OUTPUT); pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
  stop_motor();

  Serial.println("=================================");
  Serial.println("차량 시스템 시작");
  Serial.println("=================================");

  // ==================================================
  // LCD 초기화 (가장 먼저 실행, 1행만 사용)
  // ==================================================
  Wire.begin();
  lcd.init();
  lcd.backlight();

  Serial.println("MPU6050 통신 시도 중...");
  lcd_show_stage("MPU Connecting");
  Wire.beginTransmission(MPU_ADDR);

  byte error = Wire.endTransmission();
  if (error == 0) {
    mpu_available = true;
    Serial.println("MPU6050 : 연결됨, 캘리브레이션 시작...");
    lcd_show_stage("MPU Calibrating");
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x6B); Wire.write(0x00); Wire.endTransmission();
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x1C); Wire.write(0x08); Wire.endTransmission();
    Wire.beginTransmission(MPU_ADDR); Wire.write(0x1B); Wire.write(0x08); Wire.endTransmission();

    calibrate_imu();
    prev_time = millis();
    Serial.println("MPU6050 : 준비 완료");
    lcd_show_stage("MPU Ready");
  } else {
    Serial.println("MPU6050 : 연결 실패 (코드: " + String(error) + ")");
    lcd_show_stage("MPU Fail");
  }

  // ==================================================
  // ESP-01 스마트 대기 및 핸드쉐이크 로직
  // ==================================================
  Serial.println("\nESP-01 Wi-Fi 설정 시도 중...");
  lcd_show_stage("WiFi Connecting");
  
  espSerial.listen();
  delay(2000); 
  espSerial.println("CONFIG,3F_302,0424719222!!,192.168.0.164");

  bool wifi_connected = false;
  unsigned long start_wait = millis();
  
  while (millis() - start_wait < 15000) { 
    if (espSerial.available()) {
      String response = espSerial.readStringUntil('\n');
      response.trim();
      
      if (response.startsWith("OK,")) {
        int first_comma = response.indexOf(',');
        int second_comma = response.indexOf(',', first_comma + 1);
        
        String connected_ssid = response.substring(first_comma + 1, second_comma);
        String pc_ip = response.substring(second_comma + 1);
        
        Serial.println("=================================");
        Serial.println(" [성공] Wi-Fi 연결");
        Serial.println(" [설정] 와이파이 : " + connected_ssid);
        Serial.println(" [설정] PC IP : " + pc_ip);
        Serial.println("=================================");
        wifi_connected = true;
        lcd_show_stage("WiFi Connected");
        break; 
      } 
      else if (response == "FAIL") {
        Serial.println(" [실패] Wi-Fi 연결 실패");
        lcd_show_stage("WiFi Failed");
        break;
      }
    }
  }

  if (!wifi_connected) {
    Serial.println(" [오류] ESP-01 응답 없음");
    lcd_show_stage("WiFi Timeout");
  }

  btSerial.listen();
  Serial.println("HC-06 : READY");
  Serial.println("차량 READY\n");
  lcd_show_stage("Cart Ready");
  delay(1000);   // "Cart Ready" 문구를 잠깐 보여준 뒤 전환

  // ==================================================
  // 여기서부터 평소 운행 화면(Status/Driving)으로 전환
  // 연결 성공/실패 여부와 무관하게 확인 절차가 끝나면 전환됨
  // ==================================================
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Status: NORMAL");
  lcd.setCursor(0, 1);
  lcd.print("Driving: 0");
  prev_cart_status = "NORMAL";
  prev_driving_state = 0;
}

// ==================================================
// loop
// ==================================================
void loop()
{
  unsigned long current_time = millis();

  if (mpu_available) {
    read_and_filter_imu();
    check_safety_status();
    update_lcd();
  }

  if (current_time - last_send_time >= PACKET_INTERVAL_MS) {
    send_cart_packet();
    last_send_time = current_time;
  }

  if (current_time - last_ultrasonic_time >= ULTRASONIC_INTERVAL) {
    last_ultrasonic_time = current_time;
    distance = get_distance();
    obstacle_stop = (distance > 0 && distance < LIMIT_OBSTACLE_CM);

    if (current_motion == FORWARD && obstacle_stop) {
      Serial.println("!!! 장애물 감지 → 즉시 정지 !!!");
      stop_motor();
    }
  }

  if (btSerial.available()) {
    String cmd = btSerial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "A" || cmd == "a" || cmd == "A0") {
      driving_state = 1;
      update_lcd();
    }
    else if (cmd == "P" || cmd == "p" || cmd == "P0") {
      driving_state = 0;
      update_lcd();
    }
    else if (cmd == "F" || cmd == "f" || cmd == "F0") {
      if (obstacle_stop) stop_motor(); else handle_forward_command();
    }
    else if (cmd == "B" || cmd == "b" || cmd == "B0") handle_backward_command();
    else if (cmd == "L" || cmd == "l" || cmd == "L0") handle_left_command();
    else if (cmd == "R" || cmd == "r" || cmd == "R0") handle_right_command();
    else if (cmd == "X" || cmd == "x" || cmd == "X0") stop_motor();
  }

  update_direction_change();

  if (mpu_available && current_motion == FORWARD && speed > 0 && !changing_direction && !obstacle_stop)
  {
    correct_forward_direction();
  }
}
