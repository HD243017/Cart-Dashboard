#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// 1. 디폴트(기본) 셋팅 (아무 명령이 없을 때 기본으로 잡는 설정)
String current_ssid = "3F_302";
String current_password = "0424719222!!";
String current_pc_ip = "192.168.0.164"; 
int udp_port = 5000;

WiFiUDP udp;

// 💡 와이파이 접속 전용 함수
void connectToWiFi() {
  WiFi.disconnect(); // 기존 연결이 있다면 끊고 새롭게 시작
  WiFi.begin(current_ssid.c_str(), current_password.c_str());
  
  long start_time = millis();
  // 최대 10초간 접속 시도
  while (WiFi.status() != WL_CONNECTED && millis() - start_time < 10000) {
    delay(500);
  }
  
  // 접속 성공 시 UDP 포트 개방
  if(WiFi.status() == WL_CONNECTED) {
    udp.begin(udp_port);
  }
}

void setup() {
  Serial.begin(9600);
  connectToWiFi(); // 켜지자마자 디폴트 값으로 우선 접속 시도
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    
    if (line.length() == 0) return;

    // ========================================================
    // 2. 초기 셋팅 감지 로직 (헤더가 "CONFIG"인지 확인)
    // 아두이노 전송 포맷: CONFIG,와이파이이름,비번,PC아이피
    // ========================================================
    if (line.startsWith("CONFIG,")) {
      // 쉼표(,)를 기준으로 데이터 쪼개기
      int first_comma = line.indexOf(',');
      int second_comma = line.indexOf(',', first_comma + 1);
      int third_comma = line.indexOf(',', second_comma + 1);

      // 데이터가 정상적으로 4조각으로 왔다면
      if (first_comma > 0 && second_comma > 0 && third_comma > 0) {
        current_ssid = line.substring(first_comma + 1, second_comma);
        current_password = line.substring(second_comma + 1, third_comma);
        current_pc_ip = line.substring(third_comma + 1);
        
        // 새롭게 받은 정보로 와이파이 즉시 재접속!
        connectToWiFi();
      }
    } 
    // ========================================================
    // 3. 일반 데이터 통신 (IMU 등 필요한 데이터)
    // ========================================================
    else {
      // 와이파이가 연결되어 있을 때만 데이터를 UDP로 냅다 쏨
      if (WiFi.status() == WL_CONNECTED) {
        udp.beginPacket(current_pc_ip.c_str(), udp_port);
        udp.print(line);
        udp.endPacket();
      }
    }
  }
}