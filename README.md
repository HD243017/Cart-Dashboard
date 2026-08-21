# 🚚 지능형 적재 및 자세 제어 물류 이송 로봇

> **온디바이스 엣지 AI 기반 실시간 화물 객체 탐지·카운팅 및 능동형 수평 유지 자율주행 관제 시스템**

---

## 📑 목차
- [프로젝트 정보](#프로젝트-정보)
- [시작 가이드](#시작-가이드)
- [주요 기능](#주요-기능)
- [기술 스택](#기술-스택)

---

## 프로젝트 정보

### 📌 지능형 적재 및 경사 제어 물류 이송 로봇

### 🖥️ GUI 화면
<!-- 관제 화면 스크린샷 또는 데모 GIF를 docs/assets 경로에 추가 후 연결 -->
![Web GUI Dashboard](docs/assets/gui_dashboard.png)

### 📖 개요
물류 이송 과정에서 발생할 수 있는 적재물 손실 및 낙하를 방지하고, 실시간 재고 파악과 원격 관제를 지원하는 **스마트 화물 이송 모빌리티 시스템**입니다.  

차량에 탑재된 초경량 온디바이스 비전 AI를 통해 적재된 화물의 종류와 수량을 실시간으로 추론하며, 경사로 주행 시 능동적으로 짐칸 각도를 제어하여 화물의 안정성을 극대화합니다. 또한, 통합 웹 대시보드를 통해 실시간 영상 스트리밍, 물품 통계 모니터링, 차량 원격 제어를 수행할 수 있습니다.

### 🔗 배포 및 시연 링크
* **웹 대시보드:** `http://<ESP32_IP_주소>`
* **시연 영상:** [YouTube 시연 링크]

### 📅 개발 기간
* **2026.08.13 ~ 2026.08.28**

---

## 시작 가이드

### 1. 사전 요구사항
* **개발 환경:** Arduino IDE (v2.0 이상) 또는 VS Code (PlatformIO)
* **ESP32 필수 라이브러리:**
  * `ESP32 Board Package` (by Espressif)
  * `Edge Impulse Inferencing Library` (C++ Zip Library)
  * `MPU6050` / `I2Cdev`
  * `ESP32Servo`
* **관제 서버 환경:** Python 3.9 이상

### 2. 설치 및 실행

#### (1) 관제 서버 및 대시보드 실행
```bash
# 저장소 복제
git clone [https://github.com/HD243017/Cart-Dashboard.git](https://github.com/HD243017/Cart-Dashboard.git)
cd Cart-Dashboard

# 필수 패키지 설치
pip install -r requirements.txt

# 서버 실행
python main.py
```

## 주요 기능


## 기술 스택
