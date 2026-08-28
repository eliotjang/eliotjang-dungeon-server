# 테스트 시나리오 정리

서버 검증은 두 축으로 진행한다.

- GTest (tests/) : 커널 없이 도는 순수 로직 검증 (링버퍼, 프레이밍). 매 빌드마다 실행
- 봇 클라이언트 (botclient/) : 커널 경로 포함 통합 검증 (epoll, 부분 write, 백프레셔)

## 커널 버퍼 노브

기본 커널 버퍼는 자동 튜닝으로 수 MB까지 커져서, 루프백 테스트로는 EAGAIN이나 버퍼 초과가 재현되지 않는다.
서버 실행 인자로 커널 버퍼를 줄여서 같은 바이너리로 결정적 재현을 만든다.

```bash
./build/debug/ejd_server [sndbuf] [rcvbuf]   # 0 또는 생략 : 커널 기본값 유지
```

- sndbuf 축소 : write 부분 성공 / EAGAIN 재현 (EPOLLOUT 등록 --> 드레인 --> 해제)
- rcvbuf 축소 : 수신버퍼 초과 컷 재현
- 요청값과 실효값은 다르다 : 커널이 요청값을 2배로 잡고 하한 보정 (요청 1 --> 실효 4608(SNDBUF) / 2304(RCVBUF) 관측)
- 실효값은 서버 기동 로그로 확인

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
| ejd_server 1 4096 | ejd_bot bomb 10 | 비정상 클라 차단 (버퍼 초과 컷) | bomb ok: 10/10 + 서버 로그 버퍼 초과/closed |
| ejd_server 1 4096 | bomb 3 + echo 50 동시 실행 | 컷 세션과 정상 세션의 격리 | echo ok: 50/50 + 서버 생존 |

## 컷의 두 종류

- 수신버퍼 초과 (64KB) : 서버 처리 속도보다 빨리 보내는 폭주 송신자 차단
- 송신버퍼 초과 (256KB) : 에코를 읽지 않는 느린 수신자 차단
- bomb는 둘 다에 해당해서 어느 쪽이 먼저 걸릴지는 타이밍에 따라 다르다. 사유는 서버 로그로 확인

### 알려진 한계 (로드맵)

현재 구현은 rcvbuf를 아무리 줄여도 수신 컷이 지배적이다.

- 원인 : OnReadable의 read 루프가 EAGAIN까지 도는 동안 봇이 커널 수신버퍼를 계속 리필해서, 추출이 실행되지 못한 채 수신 링버퍼가 차버림. 한 fd가 이벤트 루프를 독점하는 공정성 문제이기도 함
- 해법 방향 : read 루프를 링버퍼 여유 기준으로 중단하고 수신 배압을 TCP 윈도우로 전가. 적용하면 수신 킥이 제거되고 송신 컷의 선별 검증이 가능해짐

## 관측 도구

- `ss -tn` : 커널 버퍼 적체 확인 (서버 행 Send-Q / 봇 행 Recv-Q). 주의 : Send-Q가 고정돼 있어도 유저스페이스 송신 링버퍼가 커널을 재충전하는 중일 수 있음
- stdout을 파일로 리다이렉트하면 전량 버퍼링되어 kill 시 로그가 유실됨 : `stdbuf -oL ./build/debug/ejd_server ...` 로 우회
- gdb로 bomb 디버깅 시 SIGPIPE 정지가 보이면 : launch.json setupCommands에 `handle SIGPIPE nostop noprint pass` 추가 (현재는 MSG_NOSIGNAL이라 발생하지 않는 것이 정상)
