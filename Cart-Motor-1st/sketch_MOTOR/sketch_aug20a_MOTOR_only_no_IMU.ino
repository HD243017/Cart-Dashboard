#include <IRremote.hpp>

// ==================================================
// 모터 핀
// ==================================================

int ENA = 11;
int IN1 = 10;
int IN2 = 9;

int ENB = 6;
int IN3 = 5;
int IN4 = 4;


// ==================================================
// IR 리시버
// ==================================================

int IR_PIN = 2;


// ==================================================
// 초음파 센서
// ==================================================

int TRIG_PIN = 7;
int ECHO_PIN = 8;


// ==================================================
// 모터 속도
// ==================================================

int motor_speed = 255;


// ==================================================
// 장애물 안전거리
// ==================================================

int LIMIT_OBSTACLE_CM = 15;


// ==================================================
// 초음파 측정 주기
// ==================================================

unsigned long last_ultrasonic_time = 0;

const unsigned long ULTRASONIC_INTERVAL = 50;


// ==================================================
// 현재 측정 거리
// ==================================================

int distance = -1;


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
// 리모컨 명령
// ==================================================

int IR_FORWARD = 0x18;
int IR_BACKWARD = 0x52;
int IR_LEFT = 0x08;
int IR_RIGHT = 0x5A;
int IR_STOP = 0x1C;


// ==================================================
// 자동 정지 여부
// ==================================================

bool obstacle_stop = false;


// ==================================================
// setup
// ==================================================

void setup()
{
  Serial.begin(115200);

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

  // IR
  IrReceiver.begin(IR_PIN, ENABLE_LED_FEEDBACK);

  stop_motor();

  Serial.println("=================================");
  Serial.println("차량 디버깅 시작");
  Serial.println("=================================");
}


// ==================================================
// loop
// ==================================================

void loop()
{
  unsigned long current_time = millis();


  // =================================================
  // ① 초음파 측정
  // =================================================

  if (current_time - last_ultrasonic_time >= ULTRASONIC_INTERVAL)
  {
    last_ultrasonic_time = current_time;

    distance = get_distance();

    // ▼ 대시보드로 보낼 거리값 패킷 (이 줄 추가)
    Serial.print("DIST,");
    Serial.println(distance);


    // -----------------------------------------------
    // 거리 상태 확인
    // -----------------------------------------------

    if (distance > 0 && distance < LIMIT_OBSTACLE_CM)
    {
      obstacle_stop = true;
    }
    else
    {
      obstacle_stop = false;
    }


    // -----------------------------------------------
    // 현재 전진 중이고 장애물 발견
    // -----------------------------------------------

    if (current_motion == FORWARD && obstacle_stop)
    {
      Serial.println("!!! 장애물 감지 → 자동 정지 !!!");

      stop_motor();

      current_motion = STOP;
    }


    // -----------------------------------------------
    // 디버깅 정보 출력
    // -----------------------------------------------

    Serial.print("거리 = ");

    if (distance == -1)
    {
      Serial.print("측정 실패");
    }
    else
    {
      Serial.print(distance);
      Serial.print(" cm");
    }

    Serial.print(" | 상태 = ");
    print_motion(current_motion);

    Serial.print(" | 장애물 = ");

    if (obstacle_stop)
    {
      Serial.println("YES");
    }
    else
    {
      Serial.println("NO");
    }
  }


  // =================================================
  // ② IR 리모컨 확인
  // =================================================

  if (IrReceiver.decode())
  {
    int command = IrReceiver.decodedIRData.command;


    Serial.print("IR 명령 수신: 0x");
    Serial.println(command, HEX);


    // =================================================
    // 전진
    // =================================================

    if (command == IR_FORWARD)
    {
      Serial.println("→ 전진 버튼");


      // 현재 거리 확인
      if (distance >= LIMIT_OBSTACLE_CM)
      {
        Serial.println("→ 전진 허용");

        forward();

        current_motion = FORWARD;
      }
      else
      {
        Serial.println("→ 전진 차단!");

        stop_motor();

        current_motion = STOP;
      }
    }


    // =================================================
    // 후진
    // =================================================

    else if (command == IR_BACKWARD)
    {
      Serial.println("→ 후진 버튼");

      backward();

      current_motion = BACKWARD;
    }


    // =================================================
    // 좌회전
    // =================================================

    else if (command == IR_LEFT)
    {
      Serial.println("→ 좌회전 버튼");

      left();

      current_motion = LEFT;
    }


    // =================================================
    // 우회전
    // =================================================

    else if (command == IR_RIGHT)
    {
      Serial.println("→ 우회전 버튼");

      right();

      current_motion = RIGHT;
    }


    // =================================================
    // 정지
    // =================================================

    else if (command == IR_STOP)
    {
      Serial.println("→ 정지 버튼");

      stop_motor();

      current_motion = STOP;
    }


    // 다음 IR 신호 준비
    IrReceiver.resume();
  }
}


// ==================================================
// 초음파 거리 측정
// ==================================================

int get_distance()
{
  long duration;


  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);


  duration = pulseIn(ECHO_PIN, HIGH, 30000);


  if (duration == 0)
  {
    return -1;
  }


  int distance = duration * 0.034 / 2;

  return distance;
}


// ==================================================
// 상태 출력
// ==================================================

void print_motion(Motion motion)
{
  switch (motion)
  {
    case STOP:
      Serial.print("STOP");
      break;

    case FORWARD:
      Serial.print("FORWARD");
      break;

    case BACKWARD:
      Serial.print("BACKWARD");
      break;

    case LEFT:
      Serial.print("LEFT");
      break;

    case RIGHT:
      Serial.print("RIGHT");
      break;
  }
}


// ==================================================
// 전진
// ==================================================

void forward()
{
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);

  analogWrite(ENA, motor_speed);
  analogWrite(ENB, motor_speed);
}


// ==================================================
// 후진
// ==================================================

void backward()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);

  analogWrite(ENA, motor_speed);
  analogWrite(ENB, motor_speed);
}


// ==================================================
// 좌회전
// ==================================================

void left()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);

  analogWrite(ENA, motor_speed);
  analogWrite(ENB, motor_speed);
}


// ==================================================
// 우회전
// ==================================================

void right()
{
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);

  analogWrite(ENA, motor_speed);
  analogWrite(ENB, motor_speed);
}


// ==================================================
// 정지
// ==================================================
    
void stop_motor()
{
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW); 
}