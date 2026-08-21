# eliotjang-dungeon-server

Linux C++20 게임 서버 — raw epoll 네트워크 코어를 시스템콜 레벨부터 직접 구현하는 포트폴리오 프로젝트입니다.

- **네트워크**: epoll(LT) 리액터, non-blocking 소켓, RAII fd 관리, 링버퍼, 길이 프리픽스 프레이밍, 악성 입력 방어
- **검증**: GTest 12종(경계·악성 케이스), 봇 클라이언트 세션 격리 검증, 동접 약 2만8천 세션 실측
- **다음 단계** (설계 완료, 구현 진행 중): 채널 샤딩 로직 스레드 + MPSC 큐, MySQL DB 워커(RAII 트랜잭션·세대 ID·FOR UPDATE 정산)

상세 설계와 결정 근거, 트러블슈팅 기록은 **[docs/portfolio.md](docs/portfolio.md)** 참조.

## 빌드·실행 (Linux / WSL2 Ubuntu 24.04)

요구: gcc 13+, cmake 3.21+, ninja-build (docker는 MySQL 환경용 — 현 단계 서버 구동에는 불필요)

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug              # 단위 테스트 12종
```

```bash
./build/debug/ejd_server &        # 서버 (포트 5555)
./build/debug/ejd_bot 500         # 봇 500세션 에코 검증 → "ok: 500, conns.size(): 500"
```

```bash
docker compose up -d              # MySQL 8.4 (sql/ 자동 시드, 로컬 개발 전용 크리덴셜)
```

## 구조

```
src/net/     epoll 리액터, 세션, 링버퍼, 프레이밍, fd RAII (소켓 계층)
src/proto/   패킷 헤더 정의
src/common/  버전 등 공용 유틸
botclient/   부하·검증 봇 (블로킹 소켓)
tests/       GTest (소켓 없이 검증)
sql/         스키마 시드
```

## 컨벤션

Google C++ Style Guide + C++ Core Guidelines 보완 채택 — [CONVENTIONS.md](CONVENTIONS.md) 참조 (커밋 규칙 포함)
