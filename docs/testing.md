# 테스트 시나리오 정리

서버 검증은 두 축으로 진행한다.

- GTest (tests/) : 커널 없이 도는 순수 로직 검증 (링버퍼, 프레이밍). 매 빌드마다 실행
- 봇 클라이언트 (botclient/) : 커널 경로 포함 통합 검증 (epoll, 부분 write, 백프레셔)

## 커널 버퍼 크기 설정

기본 커널 버퍼는 자동 튜닝으로 수 MB까지 커져서, 루프백 테스트로는 EAGAIN이나 버퍼 초과가 재현되지 않는다.
서버 실행 인자로 커널 버퍼를 줄여서 같은 바이너리로 결정적 재현을 만든다.

```bash
./build/debug/ejd_server [sndbuf] [rcvbuf]   # 0 또는 생략 : 커널 기본값 유지
```

- sndbuf 축소 : write 부분 성공 / EAGAIN 재현 (EPOLLOUT 등록 --> 드레인 --> 해제)
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
| echo (기본) | 커넥션별 패킷 1회 왕복 | msg_id + 페이로드 내용 일치 |
| drain | 최대 크기 패킷 4개를 read 없이 전량 송신 후 전량 수신 | 내용 일치 + 서버 로그에 EPOLLOUT 등록/해제 쌍 |
| bomb | 세션당 300KB를 read 없이 송신 후 끊김 대기 (SO_RCVTIMEO 500ms) | 전 세션이 유한 시간 내 서버에 의해 컷 |

종료 코드 : 전부 통과 0, 하나라도 실패 1 (스크립트 연쇄용)

bomb의 컷 판정은 송신 실패가 아니라 끊김 관측으로 한다.
서버가 close해도 봇의 send는 봇 커널 송신버퍼로 계속 성공하기 때문 (send 성공 != 전송 완료).
송신 상한(300KB) 도달 후 read로 대기하다가 0(FIN) 또는 ECONNRESET(RST)이 오면 컷 확인, EAGAIN(타임아웃)이면 실패.

## 시나리오 조합표

| 서버 실행 | 봇 실행 | 재현 대상 | 통과 증거 |
|---|---|---|---|
| ejd_server | ejd_bot echo 100 | 평상시 에코 경로 | echo ok: 100/100 |
| ejd_server 1 | ejd_bot drain | 부분 write + EPOLLOUT 사이클 | drain ok: 4/4 + 서버 로그 등록/해제 쌍 |
| ejd_server 1 4096 | ejd_bot bomb 10 | 비정상 클라 차단 (버퍼 초과 컷) | bomb ok: 10/10(간헐적 9/10 - 알려진 한계 참조) + 서버 로그 버퍼 초과/closed |
| ejd_server 1 4096 | bomb 3 + echo 50 동시 실행 | 컷 세션과 정상 세션의 격리 | echo ok: 50/50 + 서버 생존 |

## 컷의 두 종류

- 수신버퍼 초과 (64KB) : 서버 처리 속도보다 빨리 보내는 폭주 송신자 차단
- 송신버퍼 초과 (256KB) : 에코를 읽지 않는 느린 수신자 차단
- bomb는 현 구현에서는 항상 수신 컷 먼저 발생

### 알려진 한계 (로드맵)

현재 구현은 rcvbuf를 아무리 줄여도 수신 컷이 지배적이다.

- 원인 : OnReadable의 read 루프가 EAGAIN까지 도는 동안 봇이 커널 수신버퍼를 계속 리필해서, 추출이 실행되지 못한 채 수신 링버퍼가 차버림. 한 fd가 이벤트 루프를 독점하는 공정성 문제이기도 함
- 해법 방향 : read 루프를 링버퍼 여유 기준으로 중단하고 수신 배압을 TCP 윈도우로 전가. 적용하면 수신 킥이 제거되고 송신 컷의 선별 검증이 가능해짐

`bomb 10`(다중 세션)은 간헐적을 9/10에 멈춘다. 서버 논리적 코드 오류가 아니라 컷 판정이 OS 스케줄링에 의존하는 비결정적 테스트.

- 증거 : 실패 세션이 매 실행 다름(위치 무관) / `bomb 1`(단일 세션)은 항상 토과 / 모든 실행에서 송신 컷이나 EPOLLOUT 로그 없음 (항상 수신 컷 발생)
- 원인 : 수신 컷은 링버퍼(64KB)가 차야 발동함. 링버퍼가 차는지는 read 루프가 리필되는 커널 버퍼를 쫓아가는가(컷)와 EAGAIN을 만나 추출 루프가 링을 비우는가(회피)의 경쟁임.  
         단일 세션은 IO 스레드가 그 fd만 상대하여 항상 전자 상황임. 다중 세션은 IO 스레드가 HandleWakeup과 타 세션 이벤트로 분산되고, 그 사이 봇이 블로킹되어 커널 버퍼가 비면 후자로 됨.  
         즉시 에코에서는 read -> Send가 한 자리에서 돌았지만 워커 경유 전환으로 변경되며 이슈 확인됨.
- 해법 방향 : 위 해법 방향과 마찬가지로 링버퍼 여유 기반 read 중단하면 링버퍼가 차는 일이 없어져 bomb는 송신 컷으로 결정적 재현 가능함.

TCP half-close(SHUT_WR 후 응답 대기)는 마지막 응답을 읽는다.

- 증거 : 요청 송신 + shutdown(WR) 후 recv은 0 바이트
- 원인 : read()==0(FIN)을 전제 종료로 해석하여 즉시 세션을 닫음. 완성 패킷은 워커까지 전달되지만 비동기 응답이 돌아올 때 세션이 이미 닫혀 id 조회 실패하여 폐기됨.
- 판정 : bot 시나리오는 응답을 먼저 읽고 닫으니 미발현. 해법은 세션 Draining 상태(인플라이트 응답 전달까지 close 유예).

## 종료 (graceful shutdown)

SIGINT/SIGTERM을 signalfd로 받아 시그널을 이벤트 루프에 통합한다.

- 마스크는 워커 스레드 생성 전에 걸어놔서 시그널이 워커 스레드로 들어오는 경우도 처리 보장함
- 종료 순서는 선언 역순 파괴 : reactor(세션 fd close) → shard_worker(jthread 소멸자가 request_stop 후 join) → MPSC큐, eventfd
- 검증 로그 순서 : 'signal N 수신` → `shutdown: main return` → `shard worker loop exit`
- VS CODE F5(gdb) 안의 Ctrl+C는 종료 로그 미출력. 디버거가 신호를 전달하는 것이 아니라 ptrace로 프로세스를 정지시킴.

## 관측 도구

- `ss -tn` : 커널 버퍼 적체 확인 (서버 행 Send-Q / 봇 행 Recv-Q). 주의 : Send-Q가 고정돼 있어도 유저스페이스 송신 링버퍼가 커널을 재충전하는 중일 수 있음
- stdout을 파일로 리다이렉트하면 전량 버퍼링되어 kill 시 로그가 유실됨 : `stdbuf -oL ./build/debug/ejd_server ...` 로 우회
- gdb로 bomb 디버깅 시 SIGPIPE 정지가 보이면 : launch.json setupCommands에 `handle SIGPIPE nostop noprint pass` 추가 (현재는 MSG_NOSIGNAL이라 발생하지 않는 것이 정상)
