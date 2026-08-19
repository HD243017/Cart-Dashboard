# 🚚 Smart Autonomous Cargo Vehicle (지능형 적재 및 자세 제어 물류 이송 로봇)

> **On-Device Edge AI 기반 실시간 적재물 객체 탐지/카운팅 및 능동형 화물 수평 유지(Active Cargo Leveling) 자율주행 관제 시스템**

---

## 📌 Project Overview (프로젝트 개요)

본 프로젝트는 물류 이송 과정에서 발생할 수 있는 적재물 손실 및 낙하를 방지하고, 실시간 재고 파악과 원격 관제를 지원하는 **스마트 화물 이송 모빌리티 시스템**입니다.  
차량에 탑재된 초경량 온디바이스 비전 AI를 통해 적재된 화물의 종류와 수량을 실시간으로 추론하며, 경사로 주행 시 능동적으로 짐칸의 각도를 제어하여 화물의 안정성을 극대화합니다. 또한, 통합 Web GUI를 통해 실시간 영상 스트리밍, 물품 통계 모니터링, 차량 원격 제어를 수행할 수 있습니다.

---

## 🎯 Key Features (주요 기능)

### 1. 📷 On-Device Edge AI 물품 인식 및 수량 카운팅
* **초경량 객체 검출:** Edge Impulse 기반 **FOMO(Faster Objects, More Objects)** 신경망 모델을 활용해 리소스가 제한된 마이크로컨트롤러(MCU)에서 실시간 화물 종류 및 개수 판별
* **자동 재고 집계:** 카메라 화각 내 적재된 아이템(부품/패키지 등)의 바운딩 박스 추적 및 실시간 총 수량 카운팅

### 2. ⚖️ 능동형 짐칸 수평 제어 (Active Cargo Leveling)
* **경사로 적응형 제어:** IMU 센서(자이로/가속도)를 활용해 주행 중 경사각(Pitch/Roll)을 실시간 감지
* **화물 낙하 방지:** 액추에이터/서보 모터를 통해 오르막길 및 내리막길 주행 시 짐칸의 수평 각도를 자동 보정

### 3. 🌐 실시간 통합 Web GUI 관제 시스템
* **Live Video Streaming:** 지연 시간을 최소화한 웹 기반 실시간 카메라 뷰 제공
* **Telemetry & Inventory Dashboard:** 검출된 물품 목록, 신뢰도(Confidence), 총 수량, 차량 상태 정보 실시간 표시
* **원격 수동 제어:** 무선 리모컨(RF/Bluetooth) 조작 및 Web GUI 기반 차량 이동(전진/후진/조향) 원격 명령 지원

---

## 🛠️ System Architecture & Tech Stack (기술 스택)

| 구분 | 사용 기술 / 하드웨어 |
| :--- | :--- |
| **Edge AI & Vision** | Edge Impulse (FOMO MobileNetV2), TinyML, TensorFlow Lite Micro |
| **Microcontroller / Core** | ESP32-CAM, Arduino Framework |
| **Actuators & Sensors** | 서보 모터(짐칸 각도 조절), MPU6050 (IMU 센서) |
| **Networking & Protocols** | Wi-Fi (HTTP Web Server, WebSocket/REST API), RF/BLE Controller |
| **Web Dashboard** |  |

---

## 📐 System Pipeline (시스템 동작 구조)

```text
[ 카메라 모듈 (ESP32-CAM) ] ──> [ Frame 캡처 (QVGA) ] ──> [ RGB888 전처리 ]
                                                              │
[ Web Dashboard / GUI ] <── [ WebSocket/HTTP API ] <── [ FOMO 객체/개수 추론 ]
           │
[ IMU 경사각 측정 ] ──> [ 서보 모터 PID 제어 ] ──> [ 짐칸 수평 유지 ]
