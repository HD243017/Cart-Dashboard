#include <Wire.h>
#include <SoftwareSerial.h>

// ==================================================
// 블루투스 (HC-06)
// ==================================================
SoftwareSerial btSerial(2, 3);

// UNO RX(D2) = HC-06 TX
// UNO TX(D3) = HC-06 RX

// ==================================================
// MPU6050
// ==================================================
const int MPU_ADDR = 0x68;

// MPU6050 연결 여부
bool mpu_available = false;

// 자이로 Z축 오프셋
float gyro_offset_z = 0.0;

// 현재 Yaw
float yaw_angle = 0.0;

// 전진 시작 시 기준 Yaw
float target_yaw = 0.0;

// MPU 업데이트 주기
const unsigned long MPU_INTERVAL = 20;
unsigned long last_mpu_time = 0;

// 직진 보정 설정
const float YAW_KP = 8.5;
const float YAW_DEADBAND = 0.5;

// 최대 보정값
const int MAX_YAW_CORRECTION = 90;

// ==================================================
// 모터 핀
// ==================================================
const int ENA = 11;
const int IN1 = 10;
const int IN2 = 9;

const int ENB = 6;
const int IN3 = 5;
const int IN4 = 4;

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

// 모터 정지 방지 최소 PWM
const int MIN_SPEED = 80;

// 최대 속도
const int MAX_SPEED = 255;

// 같은 방향 버튼을 다시 눌렀을 때 증가량
const int ACCEL_STEP = 20;

// 전진 ↔ 후진 방향 전환 시 자동 감속량
const int DECEL_STEP = 10;

// 자동 감속 업데이트 주기
const unsigned long DECEL_INTERVAL = 50;

// ==================================================
// 현재 속도
// ==================================================
int speed = 0;

// ==================================================
// 차량 상태
// ==================================================
enum Motion
{
  STOP,
  FORWARD,
  BACKWARD,
  LEFT,
  RIGHT
};

Motion current_motion = STOP;

// ==================================================
// 방향 전환 상태
// ==================================================
// 현재 방향을 유지하면서 자동 감속 중인지
bool changing_direction = false;

// 감속 후 이동할 방향
Motion next_motion = STOP;

unsigned long last_decel_time = 0;

// ==================================================
// setup
// ==================================================
void setup()
{
  Serial.begin(115200);
  Wire.begin();

  // MPU6050 자동 인식
  if (detect_mpu6050())
  {
    mpu_available = true;
    Serial.println("MPU6050 : 연결됨");

    init_mpu6050();
    calibrate_mpu6050();

    Serial.println("MPU6050 : 준비 완료");
  }
  else
  {
    mpu_available = false;
    Serial.println("MPU6050 : 연결되지 않음");
    Serial.println("MPU 없이 계속 실행합니다.");
  }

  // HC-06
  btSerial.begin(9600);
  btSerial.listen();

  // 모터 A
  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  // 모터 B
  pinMode(ENB, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // 초음파
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  stop_motor();

  Serial.println("=================================");
  Serial.println("차량 시스템 시작");
  Serial.println("HC-06 : READY");
  Serial.println("=================================");
}

// ==================================================
// loop
// ==================================================
void loop()
{
  unsigned long current_time = millis();

  // ① MPU6050 업데이트
  if (mpu_available)
  {
    if (current_time - last_mpu_time >= MPU_INTERVAL)
    {
      last_mpu_time = current_time;
      update_mpu6050();
    }
  }

  // ② 초음파 측정
  if (current_time - last_ultrasonic_time >= ULTRASONIC_INTERVAL)
  {
    last_ultrasonic_time = current_time;
    distance = get_distance();

    if (distance > 0 && distance < LIMIT_OBSTACLE_CM)
    {
      obstacle_stop = true;
    }
    else
    {
      obstacle_stop = false;
    }

    if (current_motion == FORWARD && obstacle_stop)
    {
      Serial.println("!!! 장애물 감지 → 즉시 정지 !!!");
      stop_motor();
    }
  }

  // ③ 블루투스 명령
  if (btSerial.available())
  {
    String cmd = btSerial.readStringUntil('\n');
    cmd.trim();

    Serial.print("BT 명령 수신: ");
    Serial.println(cmd);

    if (cmd == "F" || cmd == "f" || cmd == "F0")
    {
      Serial.println("→ 전진 명령");

      if (obstacle_stop)
      {
        Serial.println("→ 전진 차단! 장애물 있음");
        stop_motor();
      }
      else
      {
        handle_forward_command();
      }
    }
    else if (cmd == "B" || cmd == "b" || cmd == "B0")
    {
      Serial.println("→ 후진 명령");
      handle_backward_command();
    }
    else if (cmd == "L" || cmd == "l" || cmd == "L0")
    {
      Serial.println("→ 좌회전 명령");
      handle_left_command();
    }
    else if (cmd == "R" || cmd == "r" || cmd == "R0")
    {
      Serial.println("→ 우회전 명령");
      handle_right_command();
    }
    else if (cmd == "X" || cmd == "x" || cmd == "X0")
    {
      Serial.println("→ X 명령 : 즉시 정지");
      stop_motor();
    }
  }

  // ④ 전진 ↔ 후진 자동 감속 처리
  update_direction_change();

  // ⑤ MPU6050 직진 보정
  if (mpu_available && current_motion == FORWARD && speed > 0 && !changing_direction && !obstacle_stop)
  {
    correct_forward_direction();
  }
}

// ==================================================
// F0 처리
// ==================================================
void handle_forward_command()
{
  if (current_motion == FORWARD)
  {
    changing_direction = false;

    speed += ACCEL_STEP;
    if (speed > MAX_SPEED) speed = MAX_SPEED;

    if (mpu_available)
    {
      correct_forward_direction();
    }
    else
    {
      forward();
    }

    Serial.print("전진 속도 증가 : ");
    Serial.println(speed);
    return;
  }

  if (current_motion == BACKWARD)
  {
    start_direction_change(FORWARD);
    return;
  }

  if (current_motion == STOP)
  {
    current_motion = FORWARD;
    changing_direction = false;
    speed = START_SPEED;

    if (mpu_available)
    {
      target_yaw = yaw_angle;
      Serial.print("직진 기준 Yaw : ");
      Serial.println(target_yaw);
    }

    forward();
    Serial.print("전진 시작 : ");
    Serial.println(speed);
    return;
  }

  current_motion = FORWARD;
  changing_direction = false;
  speed = START_SPEED;

  if (mpu_available)
  {
    target_yaw = yaw_angle;
  }

  forward();
}

// ==================================================
// B0 처리
// ==================================================
void handle_backward_command()
{
  if (current_motion == BACKWARD)
  {
    changing_direction = false;

    speed += ACCEL_STEP;
    if (speed > MAX_SPEED) speed = MAX_SPEED;

    backward();
    Serial.print("후진 속도 증가 : ");
    Serial.println(speed);
    return;
  }

  if (current_motion == FORWARD)
  {
    start_direction_change(BACKWARD);
    return;
  }

  if (current_motion == STOP)
  {
    current_motion = BACKWARD;
    changing_direction = false;
    speed = START_SPEED;

    backward();
    Serial.print("후진 시작 : ");
    Serial.println(speed);
    return;
  }

  current_motion = BACKWARD;
  changing_direction = false;
  speed = START_SPEED;
  backward();
}

// ==================================================
// 방향 전환 시작
// ==================================================
void start_direction_change(Motion target_motion)
{
  changing_direction = true;
  next_motion = target_motion;
  last_decel_time = millis();

  Serial.println("=================================");
  Serial.println("방향 전환");
  Serial.println("현재 방향 감속 시작");
  Serial.println("=================================");
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

  if (speed > 0)
  {
    if (current_motion == FORWARD) forward();
    else if (current_motion == BACKWARD) backward();

    Serial.print("방향 전환 감속 : ");
    Serial.println(speed);
    return;
  }

  speed = 0;
  stop_motor_without_reset();

  current_motion = next_motion;
  changing_direction = false;
  speed = START_SPEED;

  if (current_motion == FORWARD)
  {
    if (obstacle_stop)
    {
      stop_motor();
      Serial.println("→ 전진 전환 취소 : 장애물");
      return;
    }

    if (mpu_available)
    {
      target_yaw = yaw_angle;
      Serial.print("새로운 직진 기준 Yaw : ");
      Serial.println(target_yaw);
    }

    forward();
    Serial.println("→ 감속 완료 → 전진 시작");
    Serial.print("전진 시작 속도 : ");
    Serial.println(speed);
  }
  else if (current_motion == BACKWARD)
  {
    backward();
    Serial.println("→ 감속 완료 → 후진 시작");
    Serial.print("후진 시작 속도 : ");
    Serial.println(speed);
  }

  next_motion = STOP;
}

// ==================================================
// 좌회전 / 우회전
// ==================================================
void handle_left_command()
{
  changing_direction = false;
  current_motion = LEFT;
  speed = START_SPEED;
  left();

  Serial.print("좌회전 속도 : ");
  Serial.println(speed);
}

void handle_right_command()
{
  changing_direction = false;
  current_motion = RIGHT;
  speed = START_SPEED;
  right();

  Serial.print("우회전 속도 : ");
  Serial.println(speed);
}

// ==================================================
// 기본 모터 동작 함수
// ==================================================
void forward()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);

  analogWrite(ENA, speed);
  analogWrite(ENB, speed);
}

void backward()
{
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);

  analogWrite(ENA, speed);
  analogWrite(ENB, speed);
}

void left()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);

  analogWrite(ENA, speed);
  analogWrite(ENB, speed);
}

void right()
{
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);

  analogWrite(ENA, speed);
  analogWrite(ENB, speed);
}

void stop_motor()
{
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);

  current_motion = STOP;
  speed = 0;
  changing_direction = false;
  next_motion = STOP;
}

void stop_motor_without_reset()
{
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

// ==================================================
// 초음파 거리 측정 (타임아웃 단축 적용)
// ==================================================
int get_distance()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  // 10000us (약 1.7m) 타임아웃으로 지연 현상 방지
  long duration = pulseIn(ECHO_PIN, HIGH, 10000);

  if (duration == 0) return -1;
  return duration * 0.034 / 2;
}

// ==================================================
// MPU6050 함수
// ==================================================
bool detect_mpu6050()
{
  Wire.beginTransmission(MPU_ADDR);
  return (Wire.endTransmission() == 0);
}

void init_mpu6050()
{
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
}

void calibrate_mpu6050()
{
  long sum_z = 0;
  const int CALIBRATION_COUNT = 300;

  Serial.println("MPU6050 캘리브레이션 시작");
  Serial.println("차량을 움직이지 마세요.");

  for (int i = 0; i < CALIBRATION_COUNT; i++)
  {
    int16_t gx, gy, gz;
    read_gyro_raw(gx, gy, gz);
    sum_z += gz;
    delay(2);
  }

  gyro_offset_z = (float)sum_z / CALIBRATION_COUNT;
  yaw_angle = 0.0;
}

void read_gyro_raw(int16_t &gx, int16_t &gy, int16_t &gz)
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x43);
  Wire.endTransmission(false);

  Wire.requestFrom(MPU_ADDR, 6, true);

  if (Wire.available() >= 6)
  {
    gx = ((int16_t)Wire.read() << 8) | Wire.read();
    gy = ((int16_t)Wire.read() << 8) | Wire.read();
    gz = ((int16_t)Wire.read() << 8) | Wire.read();
  }
  else
  {
    gx = gy = gz = 0;
  }
}

void update_mpu6050()
{
  static unsigned long previous_time = 0;
  unsigned long current_time = millis();

  if (previous_time == 0)
  {
    previous_time = current_time;
    return;
  }

  float dt = (current_time - previous_time) / 1000.0;
  previous_time = current_time;

  if (dt > 0.2) dt = 0.02;

  int16_t gx, gy, gz;
  read_gyro_raw(gx, gy, gz);

  float gz_rate = ((float)gz - gyro_offset_z) / 65.5;

  if (abs(gz_rate) < 1.2) gz_rate = 0.0;

  yaw_angle += gz_rate * dt;

  if (yaw_angle > 180.0) yaw_angle -= 360.0;
  if (yaw_angle < -180.0) yaw_angle += 360.0;
}

// ==================================================
// 직진 방향 보정 (수정 완료)
// ==================================================
void correct_forward_direction()
{
  float yaw_error = target_yaw - yaw_angle;

  if (yaw_error > 180.0) yaw_error -= 360.0;
  if (yaw_error < -180.0) yaw_error += 360.0;

  if (abs(yaw_error) < YAW_DEADBAND)
  {
    forward();
    return;
  }

  int correction = (int)(YAW_KP * yaw_error);
  correction = constrain(correction, -MAX_YAW_CORRECTION, MAX_YAW_CORRECTION);

  int left_speed;
  int right_speed;

  // 보정 제어 방향 교정 (오른쪽으로 틀어지면 왼쪽 모터 증가 / 오른쪽 감소)
  left_speed = speed + correction;
  right_speed = speed - correction;

  // MIN_SPEED(80) 이하로 안 떨어지게 제한하여 모터 멈춤 현상 방지
  left_speed = constrain(left_speed, MIN_SPEED, MAX_SPEED);
  right_speed = constrain(right_speed, MIN_SPEED, MAX_SPEED);

  // 전진 핀 방향 고정 (forward 함수와 일치)
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);

  analogWrite(ENA, left_speed);
  analogWrite(ENB, right_speed);
}
