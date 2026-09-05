# 테스트 시나리오 정리

서버 검증은 세 축으로 진행한다.

- GTest (tests/) : 커널 없이 도는 순수 로직 검증(링버퍼, 프레이밍, MPSC 큐, 디스패처)과 실 MySQL을 상대하는 DB 계층 검증. 매 빌드마다 실행 — 26종
- 봇 클라이언트 (botclient/) : 커널 경로 포함 통합 검증 (epoll, 부분 write, 백프레셔)
- 수동 실험 : 봇 시나리오 밖의 엣지 (FIN / half-close, 종료 시그널)

## GTest 26종

| 스위트 | 개수 | 대상 |
|---|---|---|
| SmokeTest | 1 | 빌드·링크·GTest 연결 |
| RingBufferTest / RingBufferDeathTest | 4 + 1 | 쓰기 후 읽기 일치, 가득 참 거부, 랩어라운드, 여유 복원, 규칙 위반 시 assert 중단 |
| FramingTest | 6 | 완성 패킷 추출, 1바이트씩 수신, 3연속 패킷 순서, 악성 length 2종(0 / 4097), 랩 경계에 걸친 패킷 |
| MpscQueueTest | 7 | 비우기 내용 / 빈 큐 / 다시 비우기, 마감 타임아웃(하한 50ms·상한 2000ms 단언), 4생산자×1만 건 무손실·무중복·생산자별 순서(전체 상한 5초), Push 깨움, request_stop 깨움 |
| DispatcherTest | 2 | kEcho 디스패치, 미등록 msg_id 버림 |
| ConnectionTest | 3 | 클라이언트 라이브러리 버전 출력, `SELECT 1` 스칼라 조회(실 MySQL), 잘못된 포트(3307) 접속 실패와 에러 보고 |
| TransactionTest | 2 | 소멸자 ROLLBACK 후 COUNT 0, Commit 후 COUNT 1 (실 MySQL) |

스레드가 얽힌 테스트는 전체 대기 상한과 실패 단언을 반드시 둔다(무한 대기 금지).
시간 단언은 하한은 정확하게, 상한은 넉넉하게.

### DB 통합 테스트 정책

- mock 대신 실 MySQL을 상대한다. Transaction 소멸자의 ROLLBACK이 실제로 행을 되돌리는지는 실 서버의 `COUNT(*)`로만 확인할 수 있기 때문
- 준비 : `docker compose up -d --wait`로 ejd-mysql 기동 (127.0.0.1:3306, db ejd_game). 볼륨 최초 생성 시 sql/schema.sql 자동 시드. 시드 실패 진단·복구는 README
- MySQL 미기동 시 : TransactionTest 픽스처는 Connect 실패에서 `GTEST_SKIP()`("MySQL 미기동: docker start ejd-mysql") → 2종 Skipped. ConnectionTest.QueryScalar는 SKIP 처리가 없어 Failed(알려진 불일치). PrintVersion·ConnectWrongPort는 MySQL 없이 통과. 결과 : 23 통과 / 2 SKIP / 1 실패
- 격리 : 픽스처 SetUp에서 user1·user2 행을 미리 DELETE 후 커밋 → 재실행 가능. 통과 기준은 "2회 연속 통과"
- 롤백 확인은 트랜잭션 객체가 소멸한 뒤(내부 `{ }` 스코프 탈출 후)에 쿼리한다. 같은 세션의 열린 트랜잭션은 자신이 아직 커밋하지 않은 INSERT를 격리 수준과 무관하게 항상 보므로, 트랜잭션이 살아 있는 동안 세면 항상 1이 나온다

## 커널 버퍼 크기 설정

기본 커널 버퍼는 자동 튜닝으로 수 MB까지 커져서, 루프백 테스트로는 EAGAIN이나 버퍼 초과가 재현되지 않는다.
서버 실행 인자로 커널 버퍼를 줄여서 같은 바이너리로 항상 같은 결과가 나오게 재현한다.

```bash
./build/debug/ejd_server [sndbuf] [rcvbuf]   # 0 또는 생략 : 커널 기본값 유지
```

- sndbuf 축소 : write 부분 성공 / EAGAIN 재현 (EPOLLOUT 등록 --> 비우기 --> 해제)
- rcvbuf 축소 : 수신버퍼 초과 컷 재현
- 요청값과 실제값은 다름 : 커널이 요청값을 2배 이상 잡고 하한 보정
  - 요청 1 --> 실제 송신버퍼 : 4608 바이트 / 실제 수신버퍼 : 2304 바이트
- 실제 크기는 서버 기동 로그로 확인 가능

## 봇 모드

```bash
./build/debug/ejd_bot [mode] [count]   # count = 커넥션 수 (기본 10)
```

| 모드 | 시나리오 | 통과 기준 |
|---|---|---|
| echo (기본) | 커넥션별 세션 고유 페이로드를 1회 왕복 | msg_id + 페이로드 내용 일치 (세션 간 섞임 검출) |
| drain | 최대 크기 패킷 4개를 read 없이 전량 송신 후 전량 수신 | 내용 일치 + 서버 로그에 EPOLLOUT 등록/해제 쌍 |
| bomb | 세션당 300KB를 read 없이 송신 후 끊김 대기 (SO_RCVTIMEO 500ms) | 전 세션이 유한 시간 내 서버에 의해 컷 |

종료 코드 : 전부 통과 0, 하나라도 실패 1 (스크립트 연쇄용).
분모는 연결에 성공한 세션 수라 서버 미기동 시 echo·bomb는 "0/0"에 종료 코드 0이 나오는 허점이 있다(연결 실패는 perror 출력으로만 드러남).

bomb의 컷 판정은 send 성공 여부가 아니라 끊김 확인으로 한다.
서버가 close해도 봇의 send는 봇 커널 송신버퍼로 계속 성공하기 때문 (send 성공 != 전송 완료).
송신 중 EPIPE/ECONNRESET이 오면 그 자체를, 송신 상한(300KB) 도달 후 read로 대기하다가 0(FIN) 또는 ECONNRESET(RST)이 오면 컷 확인, EAGAIN(타임아웃)이면 실패.

봇은 커넥션을 순차로 돌리는 동접 유지 검증기이지 동시 요청 처리량 측정기가 아니다.

## 시나리오 조합표

| 서버 실행 | 봇 실행 | 재현 대상 | 통과 증거 |
|---|---|---|---|
| ejd_server | ejd_bot echo 100 | 평상시 에코 경로 | echo ok: 100/100 |
| ejd_server | ejd_bot echo 10000 | 대량 접속과 fd 누수 | echo ok: 10000/10000, 접속·종료 후 서버 fd 개수가 기동 직후와 동일(7 → 7). RSS 피크 약 3.2GB(세션당 320KB — 알려진 한계) |
| ejd_server 1 | ejd_bot drain | 부분 write + EPOLLOUT 사이클 | drain ok: 4/4 + 서버 로그 등록/해제 쌍 |
| ejd_server 1 4096 | ejd_bot bomb 10 | 비정상 클라 차단 (버퍼 초과 컷) | bomb ok: 10/10(간헐적 9/10 — 알려진 한계) + 서버 로그 버퍼 초과/closed |
| ejd_server 1 4096 | bomb 3 + echo 50 동시 실행 | 컷 세션과 정상 세션의 격리 | bomb ok: 3/3 + echo ok: 50/50 + 서버 생존 (3회 실측 통과) |
| ejd_server | nc -q0 / SHUT_WR (아래 수동 실험) | FIN과 함께 온 요청의 프레이밍 보존, half-close 응답 유실 재현 | 미등록 msg_id 로그(워커 도달), 응답 0바이트 |
| ejd_server | Ctrl+C / kill -TERM (아래 종료) | 정상 종료 순서 | 종료 로그 3줄 순서 |

## 수동 실험

### FIN / half-close

```bash
# kEcho 요청("hello") + FIN 을 함께 보냄 — 응답은 0바이트 (알려진 한계)
printf '\x0d\x00\x00\x00\x01\x00\x00\x00hello' | nc -q0 127.0.0.1 5555 | od -An -c

# 미등록 msg_id=99 + FIN — 서버 로그에 '미등록 MsgId: msg_id=99, session_id=N'
# → FIN과 같은 read 배치에 온 패킷이 프레이밍에서 유실되지 않고 워커까지 전달됨을 증명
printf '\x0d\x00\x00\x00\x63\x00\x00\x00hello' | nc -q0 127.0.0.1 5555
```

```python
# 요청 송신 후 shutdown(SHUT_WR)로 응답 대기 — recv는 0바이트
import socket, struct
s = socket.create_connection(("127.0.0.1", 5555)); s.settimeout(2)
s.sendall(struct.pack("<IHH", 8 + 5, 1, 0) + b"hello"); s.shutdown(socket.SHUT_WR)
print(len(s.recv(64)))   # 0
```

nc -q0의 FIN은 SHUT_WR와 같은 half-close다.
데이터와 FIN이 한 번의 epoll_wait 반환 안에서 처리되면 서버가 Push 직후 CloseSession으로 세션을 지워 워커 응답이 버려진다(0이 아닌 값이 나오면 데이터와 FIN이 따로 깨운 타이밍 문제).

### 종료 (graceful shutdown)

SIGINT/SIGTERM을 signalfd로 받아 시그널을 이벤트 루프에 통합한다.

- 마스크는 워커 스레드 생성 전에 건다. 새 스레드는 만든 스레드의 마스크를 물려받으므로 워커에는 시그널이 전달되지 않는다
- 종료 순서는 선언 역순 파괴 : reactor(세션 fd close) → shard_worker(jthread 소멸자가 request_stop 후 join) → eventfd, MPSC 큐
- 검증 로그 순서 : `signal N 수신, 종료 시작` → `shutdown: main return` → `shard worker loop exit`
- VS Code F5(gdb) 안의 Ctrl+C는 종료 로그 미출력. 디버거가 신호를 전달하는 것이 아니라 ptrace로 프로세스를 정지시킴. 재현은 서버 프로세스에 직접 `kill -INT`

## 컷의 두 종류

- 수신버퍼 초과 (64KB) : 서버 처리 속도보다 빨리 보내는 클라이언트 차단
- 송신버퍼 초과 (256KB) : 에코를 읽지 않는 느린 클라이언트 차단
- bomb는 현 구현에서는 확인한 모든 실행에서 수신 컷이 먼저 발생 (송신 컷·EPOLLOUT 로그 0건)

### 알려진 한계 (로드맵)

현재 구현은 rcvbuf를 아무리 줄여도 수신 컷이 대부분이다.

- 원인 : OnReadable의 read 루프가 EAGAIN까지 도는 동안 봇이 커널 수신버퍼를 계속 채워서, 추출이 실행되지 못한 채 수신 링버퍼가 차버림. 한 fd가 이벤트 루프를 혼자 차지하는 문제이기도 함
- 해법 방향 : read 루프를 링버퍼 여유 기준으로 중단하고 수신 배압을 TCP 윈도우로 넘김. 적용하면 수신 컷이 제거되고 송신 컷을 따로 검증할 수 있음

`bomb 10`(다중 세션)은 간헐적으로 9/10에 그친다. 서버 논리적 코드 오류가 아니라 컷 판정이 OS 스케줄링에 따라 달라지는 테스트다.

- 증거 : 실패 세션이 매 실행 다름(위치 무관) / `bomb 1`(단일 세션)은 6회 실행 전부 통과 / 모든 실행에서 송신 컷이나 EPOLLOUT 로그 없음 (항상 수신 컷 발생)
- 원인 : 수신 컷은 링버퍼(64KB)가 차야 걸림. 링버퍼가 차는지는 read 루프가 계속 채워지는 커널 버퍼를 따라가는가(컷)와 EAGAIN을 만나 추출 루프가 링을 비우는가(피함)의 경쟁임.  
         단일 세션은 IO 스레드가 그 fd만 상대하여 항상 전자 상황임. 다중 세션은 IO 스레드가 HandleWakeup과 타 세션 이벤트로 분산되고, 그 사이 봇이 블로킹되어 커널 버퍼가 비면 후자로 됨.  
         즉시 에코에서는 read -> Send가 한 자리에서 돌았지만 워커 경유로 전환되며 이슈 확인됨.
- 해법 방향 : 위와 같이 링버퍼 여유 기반으로 read를 중단하면 링버퍼가 차는 일이 없어져 bomb는 송신 컷으로 항상 같은 결과가 나옴.

TCP half-close(SHUT_WR 후 응답 대기)는 마지막 응답을 받지 못한다.

- 증거 : 요청 송신 + shutdown(SHUT_WR) 후 recv는 0 바이트 (위 수동 실험)
- 원인 : read()==0(FIN)을 전체 종료로 해석하여 즉시 세션을 닫음. 완성 패킷은 워커까지 전달되지만 비동기 응답이 돌아올 때 세션이 이미 닫혀 id 조회 실패로 버려짐.
- 판정 : 봇 시나리오는 응답을 먼저 읽고 닫으니 재현되지 않음. 해법은 세션 Draining 상태(처리 중인 응답 전달까지 close를 미룸).

세션당 320KB 버퍼 선할당이 동접 상한이다.

- 증거 : `echo 10000`은 통과하지만 서버 RSS 피크 3,170MB(기동 직후 3MB). `echo 30000`은 약 2만 세션 시점에 커널 OOM killer가 서버를 종료(WSL2 총 메모리 7.9GB)
- 원인 : Session이 수신 64KB + 송신 256KB 링을 접속 즉시 값-초기화해 페이지가 커밋됨. 이전 실측 28,228(클라 임시 포트 고갈)은 송신 링 도입 전 값이라 현재 코드에서는 재현되지 않음
- 해법 방향 : 송신 링 지연 할당(첫 EAGAIN 때 생성) 또는 링 크기 정책 분리, 메모리 예산을 설정값으로 노출

## 관측 도구

- `ss -tn` : 커널 버퍼에 쌓인 양 확인 (서버 행 Send-Q / 봇 행 Recv-Q). 주의 : Send-Q가 고정돼 있어도 유저스페이스 송신 링버퍼가 커널을 다시 채우는 중일 수 있음
- stdout을 파일로 리다이렉트하면 전량 버퍼링되어 kill 시 로그가 유실됨 : `stdbuf -oL ./build/debug/ejd_server ...` 로 우회
- gdb로 bomb 디버깅 시 SIGPIPE 정지가 보이면 : launch.json setupCommands에 `handle SIGPIPE nostop noprint pass` 추가 (현재는 MSG_NOSIGNAL이라 발생하지 않는 것이 정상)

### 진단 순서 — "서버가 멈춘 것처럼 보일 때"

앱 로그 → 소켓 큐 → 스레드 상태 → 백트레이스 순으로 내려간다.

1. 시간 샘플링 : 처리 카운트가 t=1/3/7/13s에서 고정이면 느린 것이 아니라 정지
2. `ss -tn` : Recv-Q=0이면 IO 스레드는 다 읽은 것
3. `ps -Lo tid,stat,wchan -p <pid>` : wchan이 futex면 대기 중이지 스핀이 아님
4. `gdb -batch -ex run -ex "thread apply all bt" --args ./build/debug/ejd_server` : yama ptrace_scope 때문에 실행 중인 프로세스에 attach가 막히면 gdb로 직접 띄운 뒤 SIGINT로 멈추고 백트레이스를 얻는다. SEGV는 `-ex run -ex bt`로 즉시 확인
