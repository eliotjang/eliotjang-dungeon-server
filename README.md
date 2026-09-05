# eliotjang-dungeon-server

Linux C++20 게임 서버 — raw epoll 네트워크 코어를 시스템콜 레벨부터 직접 구현하는 포트폴리오 프로젝트입니다.

- **네트워크**: epoll(LT) 리액터, non-blocking 소켓, RAII fd 관리, 링버퍼, 길이 프리픽스 프레이밍, 악성 입력 방어, 송신 큐 + EPOLLOUT 동적 등록, 2단 백프레셔 컷(수신 64KB / 송신 256KB)
- **스레드 파이프라인**: IO 스레드 → MPSC 큐 → 샤드 워커(N=1, 100ms 틱) → 아웃바운드 큐 + eventfd로 IO 스레드 깨움 → IO 스레드 송신. signalfd 기반 정상 종료
- **DB**: MySQL `Connection`/`Transaction` RAII(미커밋 소멸 = ROLLBACK), DB 계층 GTest 5종(실 MySQL 필요 3종)
- **검증**: GTest 26종, 봇 클라이언트 3모드(echo·drain·bomb), 동접·fd 누수·half-close 실측
- **로드맵** (설계 완료, 미구현): 샤드 N=2·크로스샤드 이동, DB 워커·커넥션 풀·세대 ID 마샬링, 로그인·채널·던전·정산, 하트비트

상세 설계와 결정 근거, 트러블슈팅 기록은 **[docs/portfolio.md](docs/portfolio.md)**, 테스트 시나리오는 [docs/testing.md](docs/testing.md) 참조.

## 빌드·실행 (Linux / WSL2 Ubuntu 24.04)

요구: gcc 13+, cmake 3.21+, ninja-build, pkg-config, libmysqlclient-dev. docker는 서버 구동에는 불필요하고 DB 통합 테스트(`ConnectionTest`/`TransactionTest`)에만 필요합니다. GTest는 최초 구성 시 FetchContent로 받습니다(네트워크 필요).

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug              # 26종 (MySQL 미기동 시: TransactionTest 2종 SKIP, ConnectionTest.QueryScalar 1종 실패, 나머지 23종 통과)
```

### MySQL (DB 통합 테스트용)

```bash
docker compose up -d --wait       # mysql:8.4, 컨테이너 ejd-mysql, db ejd_game, healthy까지 대기 (로컬 개발 전용 크리덴셜, compose 파일 평문)
ctest --preset debug              # 26종 전부 통과
```

`sql/schema.sql`(`account` 테이블)은 볼륨 최초 생성 시에만 initdb로 자동 시드됩니다. 시드가 실패한 반쪽 볼륨은 재시작해도 initdb가 다시 돌지 않습니다.

```bash
docker logs ejd-mysql | grep -E 'running /docker|ERROR'                  # 시드 실패 진단
docker compose down -v && docker compose up -d --wait                    # 볼륨 재생성
docker exec -i ejd-mysql mysql -uroot -p13504 ejd_game < sql/schema.sql   # 또는 수동 시드
```

### 서버·봇

```bash
./build/debug/ejd_server [sndbuf] [rcvbuf] &   # 포트 5555. 인자는 커널 버퍼 축소용(0/생략 = 커널 기본값)
./build/debug/ejd_bot echo 100                 # 세션 100개 에코 왕복 → "echo ok: 100/100"
./build/debug/ejd_bot drain                    # (서버 인자 1) 최대 크기 패킷 4개 몰아서 송수신 → "drain ok: 4/4" + EPOLLOUT 등록/해제 로그
./build/debug/ejd_bot bomb 10                  # (서버 인자 1 4096) 세션당 300KB 폭주 → 서버가 컷 → "bomb ok: 10/10"
```

봇 종료 코드는 전부 통과 0, 하나라도 실패 1입니다. 시나리오별 서버 인자와 통과 기준은 [docs/testing.md](docs/testing.md).

### 종료

Ctrl+C 또는 `kill -TERM <pid>`로 정상 종료합니다. 로그 3줄이 순서대로 찍힙니다.

```
signal 2 수신, 종료 시작      # SIGTERM은 15
shutdown: main return
shard worker loop exit
```

VS Code F5(gdb) 안에서 누르는 Ctrl+C는 시그널 전달이 아니라 ptrace 정지라 종료 로그가 나오지 않습니다. 디버거 아래에서는 서버 프로세스에 직접 `kill -INT`를 보내세요.

## 구조

```
src/net/     epoll 리액터, 세션, 링버퍼, 프레이밍, fd RAII (소켓 계층)
src/core/    MPSC 큐, 샤드 워커, 디스패처 (스레드 파이프라인)
src/db/      MySQL Connection / Transaction RAII
src/proto/   패킷 헤더·메시지 ID 정의
src/common/  버전 등 공용 유틸
botclient/   부하·검증 봇 (블로킹 소켓, echo / drain / bomb)
tests/       GTest 26종 (DB 통합 테스트 포함)
sql/         스키마 시드
docker-compose.yml   MySQL 8.4 (로컬 개발 전용)
```

## 컨벤션

Google C++ Style Guide + C++ Core Guidelines 보완 채택 — [CONVENTIONS.md](CONVENTIONS.md) 참조 (커밋 규칙 포함)
