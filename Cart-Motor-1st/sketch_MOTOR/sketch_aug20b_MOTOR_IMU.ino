#include <Wire.h>

// ==========================================
// [IMU 관련 상수값]
// ==========================================

const int MPU_ADDR = 0x68;

const float LIMIT_SHOCK_G = 2.5;
const float LIMIT_SLOPE_DEG = 20.0;

const unsigned long PACKET_INTERVAL_MS = 50;


// ==========================================
// [IMU에서 사용할 변수값]
// ==========================================

int16_t raw_ax, raw_ay, raw_az;
int16_t raw_gx, raw_gy, raw_gz;

float offset_gx = 0.0;
float offset_gy = 0.0;
float offset_gz = 0.0;

float pitch_angle = 0.0;
float roll_angle = 0.0;
float yaw_angle = 0.0;

float total_g = 1.0;

String cart_status = "NORMAL";

unsigned long prev_time = 0;
unsigned long last_send_time = 0;


// ==========================================
// [IMU 함수]
// ==========================================

void calibrate_imu()
{
  long sum_gx = 0;
  long sum_gy = 0;
  long sum_gz = 0;

  for (int i = 0; i < 500; i++)
  {
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


void read_and_filter_imu()
{
  unsigned long current_time = millis();

  float dt =
    (current_time - prev_time) / 1000.0;

  prev_time = current_time;


  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);

  Wire.requestFrom(MPU_ADDR, 14, true);


  raw_ax = (Wire.read() << 8 | Wire.read());
  raw_ay = (Wire.read() << 8 | Wire.read());
  raw_az = (Wire.read() << 8 | Wire.read());

  Wire.read();
  Wire.read();

  raw_gx = (Wire.read() << 8 | Wire.read());
  raw_gy = (Wire.read() << 8 | Wire.read());
  raw_gz = (Wire.read() << 8 | Wire.read());


  float ax = (float)raw_ax / 8192.0;
  float ay = (float)raw_ay / 8192.0;
  float az = (float)raw_az / 8192.0;


  float gx_rate =
    ((float)raw_gx - offset_gx) / 65.5;

  float gy_rate =
    ((float)raw_gy - offset_gy) / 65.5;

  float gz_rate =
    ((float)raw_gz - offset_gz) / 65.5;


  if (abs(gz_rate) < 1.2)
  {
    gz_rate = 0.0;
  }

  yaw_angle += gz_rate * dt;


  float accel_pitch =
    atan2(
      ax,
      sqrt(ay * ay + az * az)
    ) * 180.0 / PI;


  float accel_roll =
    atan2(
      ay,
      sqrt(ax * ax + az * az)
    ) * 180.0 / PI;


  pitch_angle =
    0.96 * (pitch_angle + gy_rate * dt)
    + 0.04 * accel_pitch;


  roll_angle =
    0.96 * (roll_angle + gx_rate * dt)
    + 0.04 * accel_roll;


  total_g =
    sqrt(ax * ax + ay * ay + az * az);
}


void check_safety_status()
{
  if (total_g >= LIMIT_SHOCK_G)
  {
    cart_status = "SHOCK";
  }
  else if (
    abs(pitch_angle) >= LIMIT_SLOPE_DEG ||
    abs(roll_angle) >= LIMIT_SLOPE_DEG
  )
  {
    cart_status = "SLOPE";
  }
  else
  {
    cart_status = "NORMAL";
  }
}


void send_cart_packet()
{
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
// [모터 핀]
// ==========================================

int ENA = 11;
int IN1 = 10;
int IN2 = 9;

int ENB = 6;
int IN3 = 5;
int IN4 = 4;


// ==========================================
// [IR]
// ==========================================

#include <IRremote.hpp>

int IR_PIN = 2;


// ==========================================
// [초음파]
// 실제 배선에 맞춰 확인
// ==========================================

int TRIG_PIN = 7;
int ECHO_PIN = 8;


// ==========================================
// [모터 속도]
// ==========================================

int motor_speed = 255;


// ==========================================
// [현재 차량 명령]
// ==========================================

enum Motion
{
  STOP,
  FORWARD,
  BACKWARD,
  LEFT,
  RIGHT
};

Motion current_motion = STOP;


// ==========================================
// [리모컨 명령]
// ==========================================

int IR_FORWARD = 0x18;
int IR_BACKWARD = 0x52;
int IR_LEFT = 0x08;
int IR_RIGHT = 0x5A;
int IR_STOP = 0x1C;


// ==========================================
// [초음파]
// ==========================================

int distance = -1;

const int LIMIT_DISTANCE = 15;

unsigned long last_ultrasonic_time = 0;

const unsigned long ULTRASONIC_INTERVAL = 50;


// ==========================================
// [직진 보정]
// ==========================================

float target_yaw = 0.0;

float YAW_KP = 3.0;

float YAW_DEADBAND = 2.0;


// ==========================================
// [SHOCK 잠금]
// ==========================================

bool shock_lock = false;


void setup()
{
  // ==========================================
  // IMU
  // ==========================================

  Serial.begin(115200);

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


  // IMU 캘리브레이션

  calibrate_imu();

  prev_time = millis();


  // ==========================================
  // 모터
  // ==========================================

  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  pinMode(ENB, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);


  // ==========================================
  // 초음파
  // ==========================================

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);


  // ==========================================
  // IR
  // ==========================================

  IrReceiver.begin(IR_PIN, ENABLE_LED_FEEDBACK);


  // ==========================================
  // 처음에는 정지
  // ==========================================

  stop_motor();
}


void loop()
{
  unsigned long current_time = millis();


  // ==========================================
  // ① IMU
  // 기존 코드 그대로 사용
  // ==========================================

  if (
    current_time - last_send_time
    >= PACKET_INTERVAL_MS
  )
  {
    read_and_filter_imu();

    check_safety_status();

    send_cart_packet();

    last_send_time = current_time;
  }


  // ==========================================
  // ② SHOCK 안전 정지
  // ==========================================

  if (cart_status == "SHOCK")
  {
    stop_motor();

    current_motion = STOP;

    shock_lock = true;
  }


  // ==========================================
  // ③ 초음파
  // ==========================================

  if (
    current_time - last_ultrasonic_time
    >= ULTRASONIC_INTERVAL
  )
  {
    last_ultrasonic_time = current_time;

    distance = get_distance();


    if (
      current_motion == FORWARD &&
      distance > 0 &&
      distance < LIMIT_DISTANCE
    )
    {
      stop_motor();

      current_motion = STOP;
    }
  }


  // ==========================================
  // ④ IR 리모컨
  // ==========================================

  if (IrReceiver.decode())
  {
    int command =
      IrReceiver.decodedIRData.command;


    // ----------------------------------------
    // 전진
    // ----------------------------------------

    if (command == IR_FORWARD)
    {
      if (!shock_lock &&
          !(distance > 0 &&
            distance < LIMIT_DISTANCE))
      {
        target_yaw = yaw_angle;

        forward();

        current_motion = FORWARD;
      }
    }


    // ----------------------------------------
    // 후진
    // ----------------------------------------

    else if (command == IR_BACKWARD)
    {
      if (!shock_lock)
      {
        backward();

        current_motion = BACKWARD;
      }
    }


    // ----------------------------------------
    // 좌회전
    // ----------------------------------------

    else if (command == IR_LEFT)
    {
      if (!shock_lock)
      {
        left();

        current_motion = LEFT;
      }
    }


    // ----------------------------------------
    // 우회전
    // ----------------------------------------

    else if (command == IR_RIGHT)
    {
      if (!shock_lock)
      {
        right();

        current_motion = RIGHT;
      }
    }


    // ----------------------------------------
    // 정지
    // ----------------------------------------

    else if (command == IR_STOP)
    {
      stop_motor();

      current_motion = STOP;


      // SHOCK 잠금 해제

      shock_lock = false;
    }


    IrReceiver.resume();
  }


  // ==========================================
  // ⑤ 전진 중이면 IMU 직진 보정
  // ==========================================

  if (
    current_motion == FORWARD &&
    !shock_lock
  )
  {
    correct_forward_direction();
  }
}

  // ==========================================
  // 기존 모터 함수 그대로
  // ==========================================

void forward()
{
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);

  analogWrite(ENA, motor_speed);
  analogWrite(ENB, motor_speed);
}


void backward()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);

  analogWrite(ENA, motor_speed);
  analogWrite(ENB, motor_speed);
}


void left()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);

  analogWrite(ENA, motor_speed);
  analogWrite(ENB, motor_speed);
}


void right()
{
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);

  analogWrite(ENA, motor_speed);
  analogWrite(ENB, motor_speed);
}


void stop_motor()
{
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}


  // ==========================================
  // 직진 보정 함수
  // ==========================================

void correct_forward_direction()
{
  float yaw_error =
    target_yaw - yaw_angle;


  // 각도 범위를 -180 ~ +180으로 조정

  if (yaw_error > 180)
  {
    yaw_error -= 360;
  }

  if (yaw_error < -180)
  {
    yaw_error += 360;
  }


  // 작은 오차는 무시

  if (abs(yaw_error) < YAW_DEADBAND)
  {
    forward();

    return;
  }


  // P 제어

  int correction =
    YAW_KP * yaw_error;


  correction =
    constrain(
      correction,
      -100,
      100
    );


  int left_speed;
  int right_speed;


  // ----------------------------------------
  // 왼쪽으로 보정
  // ----------------------------------------

  if (yaw_error > 0)
  {
    left_speed =
      motor_speed - correction;

    right_speed =
      motor_speed + correction;
  }


  // ----------------------------------------
  // 오른쪽으로 보정
  // ----------------------------------------

  else
  {
    left_speed =
      motor_speed + correction;

    right_speed =
      motor_speed - correction;
  }


  left_speed =
    constrain(left_speed, 0, 255);

  right_speed =
    constrain(right_speed, 0, 255);


  // 기존 전진 방향

  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);


  analogWrite(ENA, left_speed);
  analogWrite(ENB, right_speed);
}

  // ==========================================
  // 초음파 함수
  // ==========================================

int get_distance()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);


  long duration =
    pulseIn(
      ECHO_PIN,
      HIGH,
      30000
    );


  if (duration == 0)
  {
    return -1;
  }


  return duration * 0.034 / 2;
}
