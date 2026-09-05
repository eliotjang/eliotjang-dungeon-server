# eliotjang-dungeon-server — 기술 문서

> Linux C++20 게임 서버 — raw epoll 네트워크 코어 직접 구현 (시스템콜 레벨부터)  
> 작성: 장성원 (eliotjang2@gmail.com) | 저장소: https://github.com/eliotjang/eliotjang-dungeon-server  
> 이 문서는 **v0.1.2** 제출본입니다 — 네트워크 코어 · 스레드 파이프라인(IO 스레드 + 샤드 워커 N=1) · DB 자원 안전 계층(RAII) 완료 시점(저장소 태그 `v0.1.2-submit`).  
> 구현 완료 범위와 설계 단계 범위를 구분해 두었습니다. 빌드·실행 절차와 테스트 상세는 부록에 있습니다.

## 1. 프로젝트 개요

채널 기반 온라인 게임 서버의 코어를, 프레임워크 없이 시스템콜 레벨부터 직접 구현하는 프로젝트입니다.  
실무에서는 Boost.Asio 기반 사내 엔진 위에서 개발했습니다. 그 엔진이 감싸고 있던 계층(소켓, 이벤트 루프, 버퍼, 프레이밍)을 직접 만들어 이해를 증명하는 것이 목표입니다.

이번 제출본(v0.1.2)에서 추가된 것:
- 네트워크 코어의 송신 경로 완성 — 세션별 송신 링버퍼, EPOLLOUT 동적 등록, 2단 백프레셔 컷
- IO 스레드와 샤드 워커를 잇는 스레드 파이프라인 — MPSC 큐, eventfd 응답 경로, signalfd 종료
- MySQL 커넥션·트랜잭션 RAII 계층

**요건 매핑 (상태: 구현 / 적용 / 실사례 — 설계·로드맵 항목은 표 아래)**

| 영역 | 상태 | 구현 증거 | 코드 · 테스트 |
|---|---|---|---|
| C++ · CS 지식 | 구현 | 링버퍼(head+size, tail 계산)·길이 프리픽스 프레이밍·RAII 3종(`UniqueFd`·`Connection`·`Transaction`)·템플릿 MPSC 큐·`-fno-exceptions`/`-fno-rtti` 아래 값 반환 에러 설계(`std::optional`·bool·enum)·C++20(`std::jthread`·`std::stop_token`·`condition_variable_any::wait_until(stop_token)` 대기·`std::format`, 봇의 `std::span`; `std::from_chars`·`std::optional`은 C++17) | `src/net/ring_buffer.*`·`framing.*`·`unique_fd.h`, `src/core/mpsc_queue.h`, `src/db/*` — 테스트: RingBufferTest 4 + RingBufferDeathTest 1·FramingTest 6 |
| 네트워크 · 소켓 | 구현 | raw epoll(LT) 리액터·non-blocking `accept4`/`read`/`write`·EPOLLOUT 동적 등록·해제·2단 제한(수신 64KB/송신 256KB 초과 시 컷 — 송신 컷은 현 봇 시나리오에서 미발생)·커널 버퍼 설정(SO_SNDBUF/SO_RCVBUF 실행 인자)으로 EAGAIN을 항상 재현·동접 28,228 세션 실측(즉시 에코 구현 기준, 현재 코드의 상한은 세션당 320KB 버퍼 예산) | `src/net/epoll_reactor.cc`·`session.cc`·`socket_util.cc` — 테스트: 봇 echo·drain·bomb |
| 멀티스레드 | 구현 | IO↔샤드 워커 MPSC 큐(`std::mutex` + `std::condition_variable_any`, swap으로 비우기, `std::stop_token` 대기)·eventfd 응답 경로(소켓에 쓰는 스레드는 하나)·signalfd 종료·마스크 상속(워커 생성 전 설정)·선언 역순 파괴 | `src/core/mpsc_queue.h`·`shard_worker.cc`, `src/net/epoll_reactor.cc`, `src/main.cc` — 테스트: MpscQueueTest 7(생산자 4 × 10,000건 무손실·무중복 포함)·DispatcherTest 2 |
| DB (MySQL) | 구현 | libmysqlclient 위 `Connection`(`MYSQL*` 소유)·`Transaction`(미커밋 소멸 = ROLLBACK) RAII·docker compose(mysql:8.4) + `sql/schema.sql` 시드 — 계층 단위 구현이며 서버 런타임 연결은 DB 워커 로드맵 | `src/db/connection.*`·`transaction.*`, `docker-compose.yml`, `sql/schema.sql` — 테스트: ConnectionTest 3·TransactionTest 2(이 중 3종은 실 MySQL 필요) |
| Git · 테스트 | 적용 | 커밋 컨벤션(type 영어 + 한국어 제목, 1커밋 1의미, 커밋 전 빌드·테스트 통과 원칙)·GTest 26(MySQL 기동 시 전부 통과)·봇 3모드(echo·drain·bomb)·테스트 원칙(대기 상한·실패 단언 필수) | `CONVENTIONS.md`, `tests/`, `botclient/main.cc`, `docs/testing.md` |
| 생성형 AI 활용 | 실사례 | 실무 생산성 모드(원격 개발 환경 가이드·DB 도구 연동) + 이 프로젝트에서 AI를 멘토·리뷰어 역할로 두는 검증·학습 모드(과제 설계·리뷰·엣지케이스 공격은 AI, 코드는 전량 직접 작성) | 실무 사례 4건 · 이 프로젝트의 검증 루프 사례 4건 |

- 설계·로드맵(미구현): 채널 샤딩 N=2와 크로스샤드 이동, DB 워커·커넥션 풀·세대 ID 처리, 로그인·던전·정산, 하트비트(`kPing`/`kPong` 핸들러)

### 1.1 실무 경험과의 관계

- 전 직장(소셜 앱·MMO 게임서버 2종)에서는 리드가 설계한 Boost.Asio 기반 사내 엔진 위에서 콘텐츠·DB·인프라를 개발했습니다.
- 채팅·우편·선물 등 콘텐츠를 처음부터 끝까지 구현하는 일, DB 마이그레이션 파이프라인 구축, 대규모 GTest 문화가 저의 영역이었습니다. 엔진 코어는 사용자이자 분석자였습니다.
- 본 프로젝트는 그 엔진이 감싸고 있던 계층을 밑바닥부터 다시 만들어 이해를 증명하는 작업입니다. **전 직장 코드는 참조하지 않고** 공개 자료와 원리로부터 다시 구현했습니다.
- 코드는 참조하지 않았지만 설계 감각은 이어집니다. 비동기 DB 응답을 처리할 때 객체 ID를 다시 확인해 수명을 검증하던 실무 패턴이, 이 프로젝트에서 세션 ID로 죽은 세션의 응답을 버리는 경로, 그리고 로드맵의 세대 ID 처리와 같습니다.

## 2. 아키텍처 (현재 구현)

### 2.1 패킷 플로우

에코 요청 하나가 거치는 경로를 스레드 경계 기준으로 그렸습니다.

```
[봇/클라 N] ─TCP─> [IO 스레드(main): epoll_wait — listen·세션·eventfd·signalfd]
  ├─ listen EPOLLIN → AcceptAll: accept4(SOCK_NONBLOCK) 루프(EAGAIN까지)
  │    → Session(UniqueFd) 생성 → epoll ADD(EPOLLIN) → sessions_/id_to_fd_ 등록
  ├─ 세션 EPOLLIN → Session::OnReadable
  │    ① read 루프(EAGAIN까지) → 수신 링버퍼(64KB, 초과 시 컷)
  │    ② ExtractPacket: 헤더 8B·length 검증 → 완성 패킷 잘라내기
  │       (FIN이면 남은 패킷까지 추출해 워커로 넘긴 뒤 close —
  │        단 그 응답은 세션이 이미 닫혀 버려짐, half-close 한계)
  │    → SessionPacket{session_id, packet} → ShardWorker::Push ─(A)─▶ inbound
  ├─ eventfd EPOLLIN ◀──(B) → HandleWakeup
  │    read(efd) 먼저 → outbound.TryDrain 나중
  │    → id_to_fd_ 조회(실패 = 죽은 세션 응답 버림)
  │    → Session::Send: 송신 링버퍼(256KB, 초과 시 컷)에 넣고 → write
  │    → EAGAIN이면 남은 데이터 보관 → UpdateInterest: EPOLLOUT 등록
  │    → OnWritable로 비우면 EPOLLOUT 해제 ──▶ [클라]
  └─ signalfd EPOLLIN(SIGINT/SIGTERM) → HandleSignal: running_ = false
       → Run() 반환 → main 선언 역순 파괴: reactor
         → shard_worker(jthread: request_stop → join) → event_fd
         → outbound → dispatcher → signal_fd

═══════════════════════════ 스레드 경계 ═══════════════════════════
[샤드 워커 스레드: std::jthread, N=1]
   (A) → WaitDrainUntil(inbound, stop_token, tick_at): 대기 → swap으로 비우기
   → Dispatcher::Dispatch(msg_id → 핸들러; 미등록은 로그 후 버림, 연결 유지)
   → responses → ResponseSink: outbound.Push + write(eventfd, 1) ─(B)─▶
   → now ≥ tick_at이면 tick_at += 100ms (Tick 본문은 로드맵)
```

### 2.2 스레드 모델

- 스레드 2개: main 스레드 = IO 스레드(`main`에서 `reactor.Run()`), `ShardWorker`가 소유한 `std::jthread` 1개(N=1)
- 락: MPSC 큐 2개(inbound = `ShardWorker::queue_`, outbound = `main` 소유)의 `std::mutex`뿐. 세션 맵·링버퍼·epoll 관심 집합은 IO 스레드 전용
- 현재는 두 큐 모두 생산자가 1개라, 다중 생산자 안전성은 N=2·DB 워커 로드맵 대비입니다. MpscQueueTest의 생산자 4 × 10,000건 테스트로 검증했습니다
- 소켓에 쓰는 스레드는 하나: 소켓 fd에 쓰는 스레드는 IO 스레드뿐. 워커는 outbound 큐 Push + eventfd write로 IO 스레드를 깨울 뿐 소켓은 다루지 않음
- 스레드 경계: 데이터는 MPSC 큐 2개와 eventfd로만 건너고, 종료 신호는 `std::jthread`의 `std::stop_token`으로 전달. `Dispatcher`는 `Register`가 워커 생성 전에 끝나므로 락 없이 워커만 읽음

### 2.3 계층 의존 방향 (코드 include 기준)

```
main.cc   → net·core·common (조립)
net       → core(mpsc_queue·session_packet·shard_worker), proto(packet_header)
core      → proto(messages·packet_header)
db        → libmysqlclient만 (다른 계층 include 없음)
common    → 독립
game      → 미구현(디렉토리 없음)
botclient → net/unique_fd·proto
```

- 규칙은 역방향 include 금지입니다(core가 net을, proto가 다른 계층을 include하지 않음).
- 설계 의도는 net이 core를 모르는 구조였으나, 현재 코드는 `EpollReactor`가 `ShardWorker`를 직접 참조합니다. 샤드 N=2에서 세션→샤드 라우팅을 분리할 때 함께 정리할 항목입니다.
- `db`는 `ejd_lib`에 컴파일·링크되지만 서버 실행 경로(`main.cc`)에서는 아직 쓰이지 않고 `ejd_tests`에서만 사용됩니다. 연결 지점은 로드맵의 DB 워커입니다.
- 빌드 타깃 구성과 엄격 옵션 적용 범위는 `CONVENTIONS.md`에 적었습니다(`ejd_tests`는 `-Wall -Wextra -Werror`를 유지한 채 `-fno-exceptions`/`-fno-rtti`만 제외).

### 2.4 소유권 구조

- 리액터: `sessions_`(fd → `SessionEntry{session_id, unique_ptr<Session>, registered_events}`)와 `id_to_fd_`(session_id → fd)의 이중 맵. epoll fd와 listen fd(`main`에서 `std::move`로 이전)는 `UniqueFd`로 소유
- `Session`: `UniqueFd` + 수신/송신 `RingBuffer` 소유. 종료는 epoll DEL → 두 맵 erase → `unique_ptr<Session>` 소멸 → `UniqueFd` 소멸자가 close
- `main`: signal fd·event fd를 `UniqueFd`로 소유, 리액터·워커에는 정수로만 빌려줌. `Dispatcher`·outbound 큐도 `main` 소유, 참조 주입
- `ShardWorker`: inbound 큐·`ResponseSink`·`std::jthread` 소유. `thread_`를 마지막 멤버로 선언해 소멸 시 큐보다 먼저 join
- 모든 자원 해제는 소멸자 체인으로 보장. fd 누수 없음은 실측으로 확인(접속·종료 10,500회 뒤 서버 fd 개수 7 → 7)

### 2.5 프로토콜

- `[uint32 length][uint16 msg_id][uint16 flags]` 8바이트 헤더(`pack(1)`, `static_assert`로 고정) + 페이로드
- `length`는 헤더 포함 전체 길이. 하한 8·상한 4096바이트(`kMaxPacketLength`) 위반은 `kMalformed`로 즉시 컷
- 바이트 순서는 변환 없이 호스트 순서를 그대로 씀(x86-64 리틀엔디안, 봇·서버 동일 호스트 — 다른 엔디안은 범위 밖)
- `MsgId{kEcho=1, kPing=2, kPong=3}`. 핸들러 등록은 `kEcho`(`main.cc`)

## 3. 핵심 구현과 설계 결정

각 항목은 **결정 / 선택하지 않은 대안 / 근거**로 적었습니다.

### 3.1 UniqueFd — fd 소유권의 타입화

**결정**
- `unique_ptr`의 소유권 방식을 fd에 그대로 옮긴 RAII 래퍼입니다(`src/net/unique_fd.h`). 복사 금지, 이동은 소유권 이전, 소멸은 `close`.
- 리슨·epoll·eventfd·signalfd·세션 fd를 전부 이 래퍼 하나로 관리합니다.

**선택하지 않은 대안**
- 정수 fd를 그대로 들고 에러 경로마다 `close`를 직접 호출하는 방식. 조기 return 경로 하나만 빠져도 누수가 됩니다(`CONVENTIONS.md`의 RAII 필수 규칙).

**근거**
- 이동 대입은 기존 fd를 먼저 정리한 뒤 넘겨받습니다. 이동된 뒤의 객체(`fd_ = -1`)는 소멸해도 아무 일도 하지 않습니다.
- 그래서 같은 객체를 통해 `close`가 두 번 불리는 경로는 없습니다(같은 정수 fd로 래퍼를 두 번 만드는 잘못까지 막지는 못합니다).
- `close`의 EINTR은 재시도하지 않습니다. Linux에서는 EINTR이 돌아와도 fd는 이미 해제된 상태라, 재시도하면 그 사이 다른 곳에 배정된 같은 번호의 fd를 닫을 수 있습니다.

### 3.2 epoll 레벨 트리거(LT) 선택

**결정**
- LT를 씁니다(`src/net/epoll_reactor.cc`). ET 전환은 병목이 측정으로 확인될 때의 개선 항목입니다.
- `epoll_wait` 배치 크기 `kMaxEvents = 64`는 흔히 쓰는 값입니다. 매 호출이 꽉 채워 돌아오면 조정 신호로 삼는다는 기준을 주석으로 남겼습니다.

**선택하지 않은 대안**
- ET(엣지 트리거). 깨어나는 횟수를 줄이는 최적화지만, 매 이벤트마다 EAGAIN까지 다 읽지 못하면 남은 데이터가 새 데이터가 올 때까지 다시 알려지지 않아 마지막 조각이 갇힙니다. 최적화보다 정확성을 우선했습니다.

**근거**
- LT의 비용은 알고 있습니다. `EPOLLOUT`을 항상 등록해 두면 계속 깨어나는 문제는 동적 등록으로 막았습니다. `accept4`가 EMFILE로 실패하면 리슨 fd가 계속 readable로 알려져 루프가 도는 문제는 로그 분기만 두고 한계로 남겼습니다.
- LT라서 가능한 것도 있습니다. 로드맵의 read 루프 개선(링 여유 기준으로 read를 중단)은 남은 데이터가 다음 `epoll_wait`에서 다시 알려지는 LT 동작 방식이 있어야 가능합니다.

### 3.3 이벤트 식별 — data.fd + 세션 맵 조회

**결정**
- `epoll_event`의 data에 세션 포인터 대신 fd를 넣고, 매 이벤트마다 맵을 조회합니다(`HandleSessionEvent`).
- 맵은 두 개입니다. `sessions_`(fd → 세션 ID, `unique_ptr<Session>`, 등록된 epoll 관심 집합)는 이벤트 경로가, `id_to_fd_`(세션 ID → fd)는 응답 경로가 씁니다.
- 리슨 fd·eventfd·signalfd는 맵을 거치지 않고 `Run`이 fd 값 비교로 분기합니다.

**선택하지 않은 대안**
- data에 세션 포인터를 넣는 방식. 같은 `epoll_wait` 배치 안에서 앞선 이벤트 처리가 다른 세션을 닫으면(예: 응답 전달 실패 → `CloseSession`), 닫힌 세션의 이벤트가 배치 뒤쪽에 남아 있을 때 dangling 접근이 됩니다.

**근거**
- 맵 조회 방식은 삭제된 fd의 남은 이벤트가 조회 실패로 자연히 무시됩니다. 수명 버그를 자료구조 선택으로 막은 것입니다.
- 등록은 `try_emplace`의 반환값을 `assert`로 검사해 fd·세션 ID 중복을 디버그 빌드에서 즉시 잡습니다.
- 해제는 `CloseSession` 한 곳에서 `EPOLL_CTL_DEL` → 두 맵 `erase` → `Session` 소멸(fd `close`) 순으로 끝납니다.

### 3.4 링버퍼 — 소비 O(1)의 고정 용량 순환 버퍼

**결정**
- head와 size만 상태로 두고 tail은 계산합니다. full/empty 구분 문제가 없어집니다(`src/net/ring_buffer.h`).
- 같은 클래스를 수신 버퍼(64KB = 최대 패킷 4KB의 16배)와 송신 버퍼(256KB)로 재사용하며, 세션 생성 시 고정 할당합니다(`src/net/session.h`).

**선택하지 않은 대안**
- 선형 버퍼. 소비한 앞부분을 지우거나 `memmove`로 당겨야 합니다. 읽기 위치만 옮기고 압축을 미루는 방법도 언제 압축할지 정책이 따로 필요하고 최악 비용이 남습니다.
- 순환 버퍼는 고정 용량에서 정책 없이 소비가 최악 O(1)이고, 용량 자체가 제한선이 됩니다.

**근거**
- `Write`는 여유가 모자라면 `[[nodiscard]] bool`의 `false`로 거부만 하고, 컷 여부는 호출자에게 맡깁니다. `Peek`/`Consume`의 규칙 위반은 `assert`로 잡습니다.
- 랩어라운드는 경계에서 `memcpy` 두 번으로 처리합니다.
- 경계 케이스는 GTest 5종(쓰기 후 읽기 일치, 가득 참 거부, 랩어라운드 쓰기/읽기, 소비 후 여유 복원, 규칙 위반 DeathTest)으로 고정했습니다(`tests/ring_buffer_test.cc`).

### 3.5 프레이밍 — 악성 입력 처리

**결정**
- `ExtractPacket`(`src/net/framing.cc`)은 length 검증(헤더 8바이트 미만·`kMaxPacketLength` 4KB 초과)을 본문 대기보다 먼저 합니다.
- 검증 실패(`kMalformed`)는 즉시 킥이고, 수신 링 상한(64KB) 초과도 킥입니다(`Session::OnReadable`).

**선택하지 않은 대안**
- 본문을 먼저 기다리고 나중에 검증하는 순서. length=4GB짜리 악성 헤더를 "본문 수신 중"으로 취급해 자원을 잡은 채 기다리게 됩니다.

**근거**
- 프레이밍 함수는 소켓을 모르는 함수(입력은 링버퍼와 출력 벡터뿐)입니다. 그래서 완성 패킷 1개 추출, 1바이트씩 수신, 3연속 패킷 순서, 악성 length 2종, 랩 경계에 걸친 패킷을 GTest 6종으로 검증합니다(`tests/framing_test.cc`).
- `read()==0`(FIN)은 즉시 닫지 않고, 링에 남은 완성 패킷을 전부 추출·전달한 뒤 닫습니다. FIN과 같은 read 배치에 들어온 요청이 프레이밍 단계에서 유실되던 결함의 수정입니다.
- 그 요청의 응답까지 보존하는 문제(half-close)는 아직 남아 있습니다.

### 3.6 블로킹 / 논블로킹

**결정**
- "스트림에서 메시지 복원"을 봇은 블로킹 소켓의 `ReadExact` 순차 코드로, 서버는 논블로킹 소켓 + 링버퍼 상태 보존 + 이벤트 재개로 처리합니다(`botclient/main.cc`, `src/net/session.cc`).

**선택하지 않은 대안**
- 봇까지 논블로킹으로 만드는 것. 검증 도구는 검증 대상보다 단순해야 합니다.
- 블로킹 `send`는 서버 수신 윈도우가 닫히면 자연히 멈추므로 `sleep` 없이 속도가 조절됩니다. 이 프로젝트의 검증 시나리오(순차 왕복·드레인·폭주 컷)에는 이것으로 충분합니다.
- 봇은 커넥션을 순차로 돌리므로 동접 유지 검증기이지 동시 요청 처리량 측정기는 아닙니다. 의도한 범위입니다.

**근거**
- 수신 대기 상한은 `SO_RCVTIMEO`로 겁니다(송신에는 상한이 없고, bomb는 총량 300KB에서 송신 루프를 끝냅니다).
- `ConnectTo`는 서버가 아직 리슨 전이면 ECONNREFUSED를 100ms 간격으로 최대 20회 시도해, 스크립트로 연달아 실행할 때의 기동 타이밍을 흡수합니다.
- 봇 쪽에서 주의할 점 하나. `send` 성공은 전송 완료가 아니라 봇 커널 송신 버퍼에 들어갔다는 뜻입니다. 그래서 "서버가 끊었는지"는 `send` 성공 여부가 아니라 끊김 확인으로 판정합니다. `read`가 0(FIN)이나 ECONNRESET을 돌려주면 컷이고, 송신 도중 EPIPE/ECONNRESET을 만난 경우도 같은 끊김으로 셉니다.

### 3.7 송신 경로와 백프레셔

**결정**
- `Session::Send`(`src/net/session.cc`)는 송신 링에 넣은 뒤 즉시 `write` 루프를 돌고 EAGAIN에서 멈춥니다. 평상시에는 `epoll_ctl` 호출 없이 끝납니다.
- 남은 데이터가 있으면 `EpollReactor::UpdateInterest`가 `EPOLLOUT`을 등록합니다. `OnWritable`이 비우고 나면 뒤따르는 `UpdateInterest`가 해제합니다.
- 끊긴 상대에 대한 `write`는 `SIGPIPE`를 프로세스 전역에서 무시(`src/main.cc`)했으므로 시그널이 아닌 ECONNRESET/EPIPE 에러로 돌아오고, 같은 `CloseSession` 경로로 처리됩니다.

**선택하지 않은 대안**
- `EPOLLOUT` 항상 등록. LT에서는 커널 버퍼에 여유가 있는 한 매 루프 깨어나게 됩니다.

**근거**
- 동적 등록에서 주의할 점은 관심 집합 동기화입니다. `SessionEntry::registered_events`에 "등록된 집합"을 들고 "원하는 집합"(`EPOLLIN`, `WantsWrite()`면 `EPOLLOUT` 추가)과 비교해, 달라질 때만 `EPOLL_CTL_MOD`를 호출합니다. MOD는 추가가 아니라 교체이므로 `EPOLLIN`을 항상 함께 넣습니다.
- 이 구조 도입 전에는 등록 4/해제 3처럼 로그 쌍이 맞지 않았습니다. 도입 후 drain 시나리오에서 등록/해제 쌍과 `drain ok: 4/4`로 검증했습니다. 초기 구현의 "`write` EAGAIN 시 드롭+로그" 임시 처리는 이 구조로 대체되어 제거됐습니다.
- 제한은 2단입니다. 수신 링 64KB 초과는 서버보다 빨리 보내는 클라이언트를, 송신 링 256KB 초과는 에코를 읽지 않는 느린 클라이언트(slow-read)를 끊습니다.
- 1:4 비대칭은 다운링크에 치우친 MMORPG 트래픽 성격을, 총량은 1만 세션 기준 약 3.2GB의 버퍼 예산을 근거로 했습니다(`src/net/session.h` 주석). 이 예산은 실측에서 그대로 동접 상한이 됐습니다.
- 송신 컷은 즉시 에코 구현 시점의 bomb에서 수신 컷과 섞여 확인됐습니다. 현재 워커 경유 파이프라인에서는 확인한 모든 실행에서 수신 컷이 먼저 걸려(송신 컷·`EPOLLOUT` 로그 0건) 송신 컷만 따로 검증하는 것은 로드맵입니다.

### 3.8 MPSC 큐 — mutex + condition_variable_any + swap으로 비우기

**결정**
- 생산자는 `mutex` 아래 `push_back`한 뒤 락을 풀고 `notify_one`합니다. 소비자는 `condition_variable_any::wait_until`에 조건식(`!items_.empty()`)과 `stop_token`을 걸어 기다립니다(`src/core/mpsc_queue.h`).
- 비우기(드레인)는 항목별 pop이 아니라 내부 벡터와 호출자 벡터의 `swap` 한 번입니다. IO 스레드용 `TryDrain`은 같은 `swap`을 대기 없이 합니다.

**선택하지 않은 대안**
- lock-free 큐. 측정된 병목이 없는 상태에서의 최적화이자 복잡성입니다.
- 항목별 pop. 락을 N번 잡습니다.

**근거**
- 조건식 루프가 spurious wakeup을 막고, `stop_token` 오버로드가 종료 요청을 대기의 즉시 반환으로 바꿉니다.
- `Push`는 다중 생산자에 안전하고, 비우기는 단일 소비자를 가정합니다. 현재 생산자는 inbound 큐의 IO 스레드와 outbound 큐의 워커 각 1개입니다.
- 락 구간은 `swap`뿐이고 디스패치는 락 밖에서 돕니다. IO 스레드의 `Push`가 워커의 처리 시간에 막히지 않습니다(muduo `EventLoop`가 pending functor 벡터를 `swap`하는 것과 같은 방식).
- 워커 루프는 `inbound` 벡터를 루프 밖에 두어 비운 벡터를 되돌려 주므로 두 벡터의 용량이 재사용됩니다. IO 스레드의 `HandleWakeup`은 아직 깨어날 때마다 새 벡터를 쓰며, 같은 재사용을 적용하는 것은 작은 개선 항목입니다.
- 워커 틱은 `tick_at += kTickInterval`(100ms)로 다음 시각을 계산해, 처리 시간만큼 주기가 밀리는 것을 막습니다(`src/core/shard_worker.cc`; 틱 본문은 미구현).
- 검증은 GTest 7종입니다. 생산자 4 × 10,000건 무손실·무중복·생산자별 순서 보존, 마감 시각 반환(하한은 정확히, 상한은 넉넉하게 단언), `Push`와 `request_stop` 각각이 대기자를 깨우는지를 포함합니다(`tests/mpsc_queue_test.cc`).

### 3.9 응답 경로 — 소켓에 쓰는 스레드는 하나

**결정**
- 세션 소켓 fd에 쓰는 스레드는 IO 스레드뿐입니다.
- 워커는 응답을 `outbound` 큐에 넣고 깨움 전용 eventfd에 8바이트 카운터를 `write`하는 일만 합니다(`src/main.cc`에서 `ShardWorker`에 주입하는 응답 전달 람다, 워커 쪽 이름은 `ShardWorker::deliver_`).
- 실제 `Session::Send`는 IO 스레드의 `HandleWakeup`(`src/net/epoll_reactor.cc`)이 합니다. 덕분에 `Session`에는 락이 하나도 없습니다.

**선택하지 않은 대안**
- 워커가 `Session`에 직접 `write`하는 방식. 세션 수명이 IO 스레드 소유라 fd 경합이 생기고 세션별 락이 필요해집니다.
- pipe. fd가 2개이고 바이트 스트림이라, 여러 번의 깨움을 카운터 하나로 합치는 eventfd를 택했습니다.

**근거**
- `HandleWakeup`의 순서는 `read(event_fd_)` 먼저, `TryDrain` 나중입니다. 순서가 반대면 비우기와 `read` 사이에 `Push`된 항목의 깨움 카운트가 `read`로 지워져, 그 항목은 다음 `Push`가 올 때까지 큐에 남아 있게 됩니다. `read`를 먼저 하면 그 뒤에 `Push`된 항목의 카운트가 남아 LT가 다시 깨웁니다.
- 전달은 배치 단위라 eventfd `write`는 패킷당이 아니라 배치당 1회입니다.
- eventfd는 `main`의 `UniqueFd`가 소유하고, 워커 람다는 fd 번호만 값으로 캡처하며 `write` 반환값을 검사합니다. 초기 구현은 리액터로 이동시킨 `UniqueFd`를 람다가 참조로 잡아 `write(-1)`이 조용히 실패하던 결함이 있었고, 이 구조는 그 수정 결과입니다.

### 3.10 세션 ID — fd 재사용으로 응답이 잘못 전달되는 것 방지

**결정**
- `SessionPacket`(`src/core/session_packet.h`)은 fd가 아니라 `next_session_id_`에서 1씩 증가시켜 발급한 64비트 세션 ID를 담습니다. ID는 프로세스가 사는 동안 재사용되지 않습니다.
- 응답이 돌아오면 `id_to_fd_`로 현재 fd를 찾습니다. 조회 실패는 죽은 세션의 응답을 정상적으로 버리는 경로입니다(`HandleWakeup`의 `continue`).

**선택하지 않은 대안**
- fd를 키로 쓰는 방식. 커널은 가장 낮은 빈 번호부터 배정하므로 `close` 직후의 `accept`가 같은 번호를 받기 쉽습니다. A가 끊기고 같은 번호로 B가 접속하면 워커에 있던 A의 응답이 B에게 갑니다.
- 세션 포인터를 함께 담는 방식. 닫힌 세션의 남은 응답이 dangling 접근이 됩니다.

**근거**
- "요청 시점의 ID와 응답 시점의 ID를 비교해 다르면 버린다"는 이 패턴은 로드맵의 DB 워커 세대 ID 처리와 같은 구조입니다.
- 한계도 같은 자리에 있습니다. 클라이언트가 요청 송신 직후 `shutdown(SHUT_WR)`로 송신만 닫으면, 남은 패킷을 워커에 넘긴 직후 같은 이벤트 처리 안에서 세션이 닫힙니다. 워커의 응답은 이 버리는 경로로 사라지고 클라이언트는 0바이트를 받습니다.
- 정상적으로 버린 것과 버그를 가르는 것은 세션에 "응답 대기 중(Draining)" 상태가 있느냐입니다. 이 상태의 도입은 로드맵입니다.

### 3.11 종료 — signalfd와 선언 역순 파괴

**결정**
- SIGINT/SIGTERM을 `pthread_sigmask`로 막고 `signalfd`로 받아 epoll에 등록합니다. 시그널을 다른 이벤트와 같은 방식으로 이벤트 루프에서 처리합니다(`src/main.cc`, `EpollReactor::HandleSignal`).
- 핸들러가 없으므로 async-signal-safe 제약이 없고, `epoll_wait`의 EINTR 분기에 종료를 의존하지 않습니다(루프의 EINTR 분기는 방어용).

**선택하지 않은 대안**
- 시그널 핸들러 + atomic 플래그. 핸들러 안에서 할 수 있는 일이 제한되고, 시그널이 어느 스레드에 전달되는지까지 통제해야 합니다.
- self-pipe. `signalfd`가 같은 것을 커널 기능으로 제공합니다.

**근거**
- 마스크는 워커 스레드 생성 전에 겁니다. 새 스레드는 자신을 만든 스레드의 시그널 마스크를 물려받으므로 워커에는 시그널이 전달되지 않고, 막힌 시그널은 `signalfd` 읽기로만 소비됩니다.
- 종료 순서는 코드 한 줄 없이 `main`의 선언 순서(`listen_fd` → `signal_fd` → `dispatcher` → `outbound` → `event_fd` → `shard_worker` → `reactor`)의 역순 파괴가 보장합니다.
- 리액터가 먼저 파괴되어 세션 fd가 닫히고, 다음으로 `ShardWorker`의 `jthread` 소멸자가 `request_stop` → `join`을 합니다(`stop_token`을 받은 대기가 즉시 반환되므로 `join`이 늦지 않음). 워커가 마지막까지 참조하는 `outbound`·eventfd는 그 뒤에 파괴됩니다.
- `ShardWorker` 안에서도 `jthread`를 마지막 멤버로 두어 스레드가 큐보다 먼저 `join`됩니다.
- 검증 로그는 `signal N 수신, 종료 시작` → `shutdown: main return` → `shard worker loop exit` 3줄입니다.

### 3.12 DB 자원 안전 계층 — Connection·Transaction RAII

**결정**
- `Connection`(`src/db/connection.cc`)은 `MYSQL*`을 소유합니다(생성 `mysql_init`, 소멸 `mysql_close`).
- `Transaction`(`src/db/transaction.cc`)은 생성 시 `START TRANSACTION`을 보내 `active_`를 세우고, `Commit`이 `committed_`를 세웁니다. 소멸자는 `active_ && !committed_`이면 `ROLLBACK`을 보냅니다.
- "아직 커밋되지 않은 상태"를 객체가 소유하므로, 조기 return·실패 경로는 자동으로 롤백되고 커밋 경로만 살아남습니다.

**선택하지 않은 대안**
- 각 경로에 `ROLLBACK`을 직접 쓰는 방식. 경로 하나를 빠뜨리면 곧 버그입니다.
- 예외. `-fno-exceptions` 규칙상 선택지에 없습니다.

**근거**
- `Execute`는 `mysql_real_query`에 `string_view`의 길이를 직접 넘겨 널 종단에 의존하지 않습니다. `QueryScalar`는 단일 값을 `optional<int64_t>`로 돌려 "값 없음"을 값으로 표현합니다.
- `Connect`·`Execute`·`QueryScalar`·`Commit`은 `[[nodiscard]]`라 반환값을 무시하면 `-Werror` 아래에서 컴파일 에러입니다. 소멸자의 롤백 실패는 전파할 방법이 없어 `last_errno`/`last_error`로 로그만 남깁니다.
- 예외 없는 생성자는 실패를 던질 수 없으므로 `Transaction`은 `active()`로 알립니다(`UniqueFd::valid()`와 같은 규칙).
- 불완전한 곳도 있습니다. `Connection`은 복사 생성자만 delete하고 복사 대입 연산자는 아직 delete하지 않았습니다(현재 코드에 대입 사용처는 없음). `mysql_init` 실패를 알리는 접근자도 없습니다. 둘 다 보완 항목입니다.
- 검증은 DB 계층 GTest 5종입니다. 실 MySQL 접속이 필요한 것은 스칼라 조회·소멸자 롤백·정상 커밋 3종이고, 나머지 2종(클라이언트 라이브러리 버전 출력, 잘못된 포트 접속 실패와 에러 보고)은 서버 없이 돕니다. 롤백 확인은 트랜잭션 스코프를 벗어난 뒤에 합니다(`tests/connection_test.cc`, `tests/transaction_test.cc`).

### 3.13 Dispatcher — msg_id → 핸들러 테이블

**결정**
- `Dispatcher`(`src/core/dispatcher.h`)는 `unordered_map<uint16_t, Handler>` 테이블입니다. `Register`는 `insert_or_assign`으로 등록합니다(같은 ID 재등록은 교체).
- 핸들러 시그니처는 `(const SessionPacket& in, vector<SessionPacket>& out)`입니다. 입력은 const 참조라 바꿀 수 없고, 응답은 0..N개를 `out`에 쌓습니다(응답 없음·1개·다른 세션 ID를 단 N개를 같은 형태로 표현). 현재 등록된 핸들러는 `kEcho` 하나입니다.

**선택하지 않은 대안**
- `switch` 분기. 메시지가 늘 때마다 디스패처를 고쳐야 합니다.
- 메시지별 클래스 계층에서 `dynamic_cast`로 타입을 판별하는 방식. `-fno-rtti` 규칙과 어긋납니다(`CONVENTIONS.md`: 패킷 디스패치는 타입 ID + 핸들러 테이블).

**근거**
- 새 메시지는 등록 한 줄이고 `Dispatcher`는 수정하지 않습니다(OCP). 등록은 워커 시작 전 `main`에서 끝나므로 실행 중 테이블은 읽기 전용이며 락이 없습니다.
- `Dispatch`는 헤더를 `memcpy`로 읽고 패킷 길이가 헤더 이상임을 `assert`로 전제합니다. 이는 프레이밍이 보장하는 규칙입니다.
- 미등록 `msg_id`는 현재 로그 후 버리고 연결은 유지합니다. 반복 시 킥은 로드맵입니다. `kPing`/`kPong`은 enum에만 있고 핸들러는 미등록입니다(하트비트 로드맵).
- 검증은 GTest 2종(에코 등록 후 디스패치, 미등록 시 응답 없음)입니다(`tests/dispatcher_test.cc`).

## 4. 실측과 트러블슈팅

과제 단계 순(네트워크 코어 → 송신 경로·백프레셔 → 스레드 파이프라인 → DB)으로 묶었고, 각 항목은 **증상 → 진단 → 원인 → 조치·교훈**으로 적었습니다. 수치와 로그 문구는 당시 기록 그대로입니다.

### 4.1 동접 측정 — 상한이 포트에서 메모리로 이동

- **증상**: 10만 세션 목표로 부하를 걸자 28,228 세션에서 봇의 `connect`가 `EADDRNOTAVAIL`로 실패했습니다.
- **원인**: TCP 연결은 4-튜플로 구분됩니다. 단일 클라이언트 IP에서 같은 서버로 맺을 수 있는 동시 연결 수는 임시 포트 범위(`ip_local_port_range` = 32768~60999, 28,232개)가 상한입니다.
- **판정**: 병목은 서버가 아니라 부하 생성기 쪽이었습니다. 서버는 단일 IO 스레드로 동접 약 28,000 세션을 유지했습니다. 이 값은 워커 스레드 도입 전, IO 스레드가 read 직후 바로 에코하던 구현(2026-08-20)의 측정입니다.
- **재측정(현재 코드)**: 제출 전에 현재 파이프라인(IO → 워커 → eventfd 응답)으로 다시 재 보니 상한이 다른 자원으로 옮겨 가 있었습니다.
  - `echo 10000`은 10000/10000(3.4초)으로 통과했지만, 서버 RSS 피크가 3,170MB(기동 직후 3MB)였습니다.
  - `echo 30000`은 약 2만 세션 시점에 커널 OOM killer가 서버를 종료했습니다(`dmesg`: `anon-rss:6455444kB`, WSL2 총 메모리 7.9GB).
  - 원인은 `Session`이 수신 64KB + 송신 256KB 링을 `std::vector<char>(capacity)`로 접속 즉시 값-초기화해 페이지가 실제로 커밋되는 것입니다. 세션당 320KB × 1만 세션 = 3.2GB라는 설계 시점의 버퍼 예산이 그대로 측정값이 됐습니다.
  - 이전 실측 28,228은 송신 링(256KB) 도입 전 값이라, 현재 코드에서는 같은 머신에서 재현되지 않습니다.
- **교훈·조치**: 상한은 항상 어딘가의 자원이고, 측정이 그 자원이 무엇인지(포트 → 메모리) 알려 줍니다. 수치 자체보다 실패 지점에서 OS 계층으로 내려가 원인을 확인한 과정에 의미를 둡니다. 개선 방향(송신 링 지연 할당, 링 크기 정책 분리)은 로드맵에 두었습니다.

### 4.2 "연결만 되고 응답이 없음"

- **증상**: 프레이밍 도입 직후 봇이 응답 대기에서 무한히 멈췄습니다.
- **진단**: 접속 로그는 정상 → 서버가 에코를 보내지 않음 → `OnReadable` 흐름 추적 순으로 좁혀 갔습니다.
- **원인**: read 루프의 `EAGAIN` 분기가 `break`가 아니라 `return`이어서, 그 아래의 추출·응답 단계에 도달할 수 없었습니다. `-Wall -Wextra -Werror`로도 경고는 나오지 않았습니다.
- **교훈**: 계층별 로그와 흐름 추적으로만 잡을 수 있는 유형이었습니다. 이후 "패킷이 어느 계층까지 갔는가"를 먼저 묻는 진단 습관의 출발점이 됐습니다.

### 4.3 SO_REUSEADDR가 못 뚫는 것

- **증상**: `SO_REUSEADDR`를 설정해 두었는데도 `bind: Address already in use`가 발생했습니다.
- **원인**: 디버거에 정지된 이전 서버 프로세스가 포트를 점유하고 있었습니다. `SO_REUSEADDR`는 TIME_WAIT(끝난 연결이 남긴 상태)의 재바인드를 허용할 뿐, 살아 있는 리스너는 여전히 막습니다(포트 하이재킹 방지).
- **조치**: 디버그 세션을 겹치지 않게 종료하는 습관을 들였습니다.

### 4.4 EPOLLOUT 해제 조건 반전 — "로그가 찍혀도 버그, 안 찍혀도 버그"

- **증상**: 송신 큐와 `EPOLLOUT` 동적 등록을 넣은 직후, 해제 조건의 `WantsWrite()` 판정이 반대로 되어 있었습니다. 증상은 두 가지입니다.
  - 큐가 비었는데 `EPOLLOUT`을 해제하지 않으면 LT 특성상 `epoll_wait`가 계속 깨어납니다. 봇이 응답을 읽자마자 종료해 이 증상은 가려졌습니다.
  - 남은 데이터가 있는데 해제하면 데이터가 영원히 갇힙니다.
- **진단**: 후자를 잡은 것이 `ss -tn`입니다. 서버 쪽 Send-Q=0인데 봇은 read 대기 중이었으므로, 데이터가 커널이 아니라 유저스페이스 송신 링에 갇혀 있다는 결론이 나왔습니다. 서버 인자 1로 송신버퍼를 하한 4608바이트까지 줄여 두 증상을 모두 재현했습니다.
- **조치**: `SessionEntry`에 `registered_events`를 두어 원하는 관심 집합과 등록된 집합을 비교하고, 달라질 때만 `EPOLL_CTL_MOD`하는 `UpdateInterest`로 정리했습니다. 이후 등록/해제 로그가 정확한 쌍으로 찍혔습니다.
- **덤으로 얻은 것**: 2단 큐를 읽는 법입니다. 브레이크포인트로 느린 소비자를 만들어 보면 Send-Q가 가득 찬 채 고정된 동안에도 송신 링이 커널을 다시 채우고 있고, 봇 쪽 Recv-Q 감소량은 프로토콜 상수(헤더 8, 페이로드 4088)와 1:1로 맞습니다.

### 4.5 read 루프가 추출 단계를 막는 문제 — 가설이 실측으로 뒤집힘

- **가설**: bomb 모드로 수신 컷과 송신 컷을 따로 검증하려던 중, 리뷰어(AI)가 "서버 rcvbuf를 수신 링 64KB보다 작게 잡으면 수신 컷은 구조적으로 불가능하고 송신 컷만 남는다"고 제안했습니다.
- **실측**: 반대였습니다. rcvbuf를 하한 2304바이트까지 줄여도 `수신버퍼 초과: 시도=576, 남은 공간=448` 로그가 10세션 전부에서 찍혔습니다.
- **원인**: `OnReadable`의 read 루프는 `EAGAIN`이 나올 때까지 돕니다. 봇이 블로킹 `send`로 커널 수신버퍼를 계속 채우면 한 번의 `epoll_wait` 반환 안에서 `EAGAIN`이 나오지 않습니다. 루프가 계속 채워지는 버퍼를 따라가는 동안 추출 단계가 실행되지 못해 링이 차 버립니다. 한 fd가 이벤트 루프를 혼자 차지하는 문제이기도 합니다.
- **조치**: 링 여유 기준으로 read를 멈추고 수신 배압을 TCP 윈도우로 넘기는 것이 해법 방향이며 로드맵에 두었습니다. 적용되면 수신 컷이 사라지고 송신 컷을 따로 검증할 수 있게 됩니다.

### 4.6 eventfd 깨움이 -1로 새던 사건

- **증상**: 워커 경유 파이프라인을 조립한 직후 에코가 봇에 돌아오지 않았습니다(봇은 헤더 대기에서 타임아웃).
- **진단**: gdb로 계층을 하나씩 내려갔습니다.
  - `CloseSession` 브레이크포인트는 봇 타임아웃 FIN에 따른 정상 close에 걸린 것이라 단서가 아니었습니다.
  - `HandleWakeup` 브레이크포인트는 아예 걸리지 않았습니다.
  - 워커가 eventfd에 `write`하는 지점에서 멈춰 보니 `event_fd`의 fd 값이 -1이었습니다.
- **원인**: 소유권 충돌입니다. `main`에서 `UniqueFd event_fd`를 만들고 워커의 응답 전달 람다가 `[&event_fd]`로 참조 캡처한 직후, 같은 객체를 `std::move`로 리액터에 넘겼습니다. 이동 후 `main`의 지역 변수는 -1이고 람다는 그 죽은 참조를 봤습니다. `write(-1)`은 `EBADF`인데 반환값을 검사하지 않아 조용히 실패했습니다.
- **조치·교훈**: `src/main.cc`에서 정수 fd 값을 캡처(`efd = event_fd.get()`)하고 리액터에도 정수만 넘겨 `UniqueFd`는 `main`이 소유하게 했습니다. `write` 반환값은 검사해 `perror`합니다. 참조를 잡아 둔 객체를 이동하면 그 참조는 빈 객체를 가리키므로, 공유할 자원은 이동하지 말고 값으로 빌려 주어야 합니다. 실패 보고를 무시한 시스템콜은 버그를 조용하게 만듭니다.

### 4.7 bomb 간헐 9/10의 원인 찾기

- **증상**: `bomb 10`이 5회 중 2회 `bomb ok: 9/10`으로 끝났습니다(실패 세션은 500ms 타임아웃 뒤 `컷 감지 실패`로 보고).
- **1차 가설과 그 결과**: 리뷰어는 `HandleWakeup` 비우기 루프가 죽은 세션을 만나면 `break`로 배치 전체를 중단해 살아 있는 세션의 에코를 버리고, 그래서 송신 컷이 작동하지 않는다고 봤습니다. `continue`로 고친 뒤 재실행하자 8회 중 4회 실패로 오히려 나빠졌고 `EPOLLOUT` 로그는 여전히 0이었습니다. `break`는 실제 버그였지만 이 증상의 원인은 아니었습니다.
- **근거 다시 모으기**:
  - 실패 세션이 매 실행 다르고(session 6/3/4/1), `bomb 1`(단일 세션)은 6회 실행 전부 통과했습니다.
  - 모든 실행에서 송신 컷과 `EPOLLOUT` 로그가 0이었습니다. 즉 세션은 항상 수신 컷으로만 죽었습니다.
  - 수신 컷은 링 64KB가 차야 걸리고, 링이 차는지는 read 루프와 추출 루프의 경쟁에 달려 있습니다. 다중 세션에서는 IO 스레드가 `HandleWakeup`과 다른 세션 이벤트로 분산되고, 그 사이 봇의 블로킹 `send`가 멈춰 커널 수신버퍼가 비면 `EAGAIN`이 나와 추출이 링을 비웁니다. 그래서 경쟁 결과가 실행마다 뒤집힙니다.
- **결론**: 서버의 컷 논리 오류가 아니라, read 루프 특성 때문에 컷 판정이 OS 스케줄링에 따라 달라지는 테스트입니다. 해법은 링 여유 기반 read 중단이며, 그러면 링이 차는 일이 없어져 bomb는 송신 컷으로 항상 같은 결과를 냅니다.

### 4.8 half-close에서 처리 중인 응답이 유실

- **증상**: 요청을 보낸 뒤 `shutdown(SHUT_WR)`로 응답을 기다리면 `recv`가 0바이트를 돌려줍니다.
- **진단**: 프레이밍 단계는 정상입니다. `OnReadable`은 `read()==0` 뒤에도 링의 완성 패킷을 전부 추출하고, `HandleSessionEvent`는 그 패킷을 워커 큐에 `Push`합니다(미등록 `msg_id`를 실어 보내면 워커 로그가 찍히는 것으로 확인).
  - 문제는 그다음 단계입니다. `Push` 직후 `CloseSession`이 `id_to_fd_`에서 세션을 지우므로, 워커 응답이 eventfd 경로로 돌아왔을 때 `HandleWakeup`의 id 조회가 실패해 "죽은 세션 응답"으로 버려집니다.
- **원인**: `read()==0`을 전체 종료로 해석하는 것입니다. half-close는 "송신만 닫고 수신은 계속"인데 `read()==0`만으로는 둘을 구분할 수 없습니다. 봇의 echo·drain은 응답을 먼저 읽고 닫으므로 재현되지 않고, `SHUT_WR`에서만 드러납니다.
- **해법·교훈**: 세션에 Draining 상태를 두어 처리 중인 응답이 전달될 때까지 close를 미루는 것입니다(로드맵). 던전 인스턴스 소멸 상태머신(비동기 완료 시점과 수명의 경쟁)과 같은 문제입니다. "죽은 세션 응답은 id 조회에서 자연히 버려진다"는 설계 판단이, 요청 직후의 정상 종료(요청+FIN)라는 다음 단계에서 뒤집힌 사례입니다.

### 4.9 "멈춘 서버"가 사실은 설계값이었던 사건 — 계층을 내려가는 진단 순서

- **증상**: drain 모드가 패킷 100개를 보낸다고 가정한 상태에서, 서버가 4개를 처리한 뒤 멈춘 것처럼 보였습니다.
- **진단**: 리뷰어의 서버 버그 가설에서 출발해 아래 순서로 내려갔습니다.
  - 시간 샘플링: t=1/3/7/13s에서 처리 카운트가 4로 고정. 느린 것이 아니라 정지.
  - `ss -tn`: Recv-Q=0. IO 스레드는 다 읽었음.
  - `ps -Lo wchan`: 워커는 futex 대기. 스핀 아님.
  - gdb 백트레이스: 워커는 조건변수 대기, IO는 `epoll_wait`. 둘 다 정상 유휴.
- **원인**: 봇 코드의 `constexpr int packet_count = 4;`였습니다. 서버는 받은 것의 100%를 처리했고, "100개"라는 가정이 확인 순서의 끝에서 틀렸음이 드러났습니다.
- **교훈**: 멀티스레드 서버가 멈춘 것처럼 보일 때 앱 로그 → 소켓 큐 → 스레드 상태 → 백트레이스로 내려가는 순서가 이때 습관이 됐습니다. 도구 우회도 기록해 둡니다. yama `ptrace_scope` 때문에 실행 중인 프로세스에 gdb attach가 막혀, gdb로 서버를 직접 띄운 뒤 SIGINT로 멈추고 `thread apply all bt`를 실행했습니다.

### 4.10 임시 jthread 소멸로 타임아웃 테스트가 즉시 반환

- **증상**: `WaitDrainUntil`의 타임아웃 테스트가 기다리지 않고 즉시 반환했습니다(경과 시간이 나노초 수준).
- **원인**: 테스트에서 `std::jthread([...]{...});`처럼 임시 객체를 만들었습니다. 임시 `jthread`는 문장 끝에서 소멸하며 `request_stop` → `join` 순으로 실행되므로, 내장 `stop_token`을 받은 `wait_until`이 즉시 반환한 것입니다.
- **검출**: `EXPECT_GE(elapsed.count(), 50)` 하한 단언이 잡았습니다. 상한만 두었다면 "빨리 끝났으니 통과"로 지나갔을 결함입니다. "하한은 정확히, 상한은 넉넉하게"라는 테스트 원칙이 처음 효과를 낸 사건입니다.
- **조치·교훈**: 현재 타임아웃 테스트는 `std::stop_source`를 직접 만들어 토큰을 넘깁니다. stop 요청이 대기를 즉시 끝내는 종료 경로의 단서도 여기서 얻었고, 이는 이후 `StopRequestWakesWaiter`(20ms 뒤 `request_stop`)로 따로 고정했습니다.

### 4.11 롤백 테스트가 항상 1

- **증상**: `Transaction` 소멸자 롤백 검증에서 첫 실행은 `COUNT(*)`가 항상 1이었고, 고친 뒤 재실행에서는 `INSERT`가 실패했습니다. 둘 다 구현 버그가 아니라 테스트 구조 문제였습니다.
- **원인 1·조치**: 확인 쿼리를 트랜잭션 객체가 살아 있는 시점에 실행했습니다. 같은 세션의 열린 트랜잭션은 자신이 아직 커밋하지 않은 `INSERT`를 격리 수준과 무관하게 항상 보므로, 측정 시점이 "롤백 전"이었던 것입니다. 트랜잭션과 `INSERT`를 내부 `{ }` 스코프로 감싸 소멸자(`ROLLBACK`)가 확인 쿼리보다 먼저 실행되게 했습니다.
- **원인 2·조치**: `ExpectCommit`이 커밋한 `user2` 행이 DB에 남아 재실행 시 `UNIQUE` 위반으로 `INSERT`가 실패했습니다. 픽스처 `SetUp`에서 미리 `DELETE`하도록 했고, 이후 2회 연속 통과로 재현성을 확인했습니다.
- **교훈**: "측정은 자원이 해제된 뒤에"라는 RAII 스코프 규칙이 테스트에도 그대로 적용됩니다.

## 5. AI 활용 방식

AI는 목적에 따라 활용 방식을 달리해 썼습니다. 실무에서는 생산성, 이 프로젝트에서는 실력을 내 것으로 만드는 것이 목적이며, 공통 원칙은 마지막에 적습니다.

### 5.1 실무에서의 생산성 모드

전 직장에서 AI와 관련해 한 일은 네 가지입니다. 앞의 셋은 개발 워크플로에 AI를 넣은 것이고, 마지막은 AI 서비스의 서버 기능을 만든 것입니다.

1. Cursor 원격 개발 환경(Windows→Linux 빌드 서버)을 구축하고 팀 가이드 문서로 자리 잡게 했습니다(온보딩 기여)
2. MySQL을 MCP(Model Context Protocol)로 AI에 연결해 프로시저 작성 시 실제 스키마를 참조하게 했습니다. 범위는 의도적으로 좁혔습니다. 로컬 전용 개발 DB만 연결하고(프로덕션·실유저 데이터 제외), 용도는 테이블 구조·프로시저 시그니처 참조로 한정했으며, AI가 생성한 SQL은 반드시 검토 후 실행했습니다
3. DB 마이그레이션 도구(스키마 diff → SQL 자동 생성·적용) 개발에서 Atlas 도입 검토와 엣지 케이스(프로시저 덤프의 한글 파싱, RDS 권한 제약)를 AI와 함께 풀었습니다. 단 DROP 같은 위험 DDL은 자동 적용에서 제외해 DRAFT(적용 보류)로 분리하고 사람이 승인하는 구조로 두었습니다
4. LLM 기반 서비스의 서버 측 기능(AI 사용량 제한·차감, BYOK(Bring Your Own Key) 키 관리)을 구현했습니다. AI를 '쓰는 도구'와 '만드는 기능' 양쪽 관점을 갖게 된 경험입니다

원칙은 "AI는 초안과 조사, 판단과 검증은 나"입니다. 특히 학습 데이터에 없는 사내 엔진에서는 AI 제안이 컨벤션을 어기는 경우가 잦아, 검토를 거쳐 적용했습니다.

### 5.2 이 프로젝트에서의 검증·학습 모드

목적이 실력을 쌓는 것이라서 역할을 반대로 두었습니다. AI에게는 과제 설계, 선행 지식 브리핑, 코드 리뷰, 엣지 케이스 공격을 맡기고 **코드는 전량 직접 작성**했습니다. 아래에 적은 결함들도 전부 제 손에서 나온 실수이고, 수정 이력은 커밋 히스토리에 있습니다.

검증 루프는 양방향으로 돌았습니다.

- **검증과 리뷰가 잡은 것**
  - 송신 경로·백프레셔 단계의 치명 결함 3건(EPOLLOUT 해제 조건 반전, 봇 `SendAll`의 errno 규칙 위반, bomb 반환식 괄호 오배치)은 봇 실측에서 증상으로 검출됐고, 리뷰가 원인을 Critical로 분류했습니다.
  - 스레드 파이프라인 전환에서는 `UpdateInterest` 호출이 EPOLLIN 블록 안으로 들어가 EPOLLOUT 단독 경로가 동기화되지 않는 결함을, 증상이 나타나기 전에 리뷰로 잡았습니다.
- **반대로 AI의 가설을 실측으로 뒤집은 것** — 세 건입니다.
  - "rcvbuf가 수신 링보다 작으면 수신 컷은 구조적으로 불가능하다"는 가설은 rcvbuf 2304바이트에서 10세션 전부 수신 컷이 나며 틀렸음이 확인됐습니다.
  - bomb 간헐 실패의 원인으로 지목된 `HandleWakeup`의 `break`는 `continue`로 고친 뒤에도 실패가 계속됐고(5회 중 2회 → 8회 중 4회) 송신 컷·EPOLLOUT 로그가 여전히 0이라 원인이 아니었습니다(`break`는 별개의 실제 버그).
  - drain 모드의 "멈춤"은 서버 버그 가설로 진단 순서를 끝까지 내려간 뒤 봇의 설계값 `packet_count = 4`로 판명됐습니다.
- **검증 신호 자체를 의심한 것**
  - `MpscQueue::TryDrain`의 잘못된 swap 시도가 프로젝트 빌드를 통과한 적이 있습니다. 템플릿 매개변수 `T`에 의존하는 식은 인스턴스화 시점에야 검사되므로(2단계 이름 검색), 인스턴스화하는 번역 단위(TU)가 없던 그 시점의 "빌드 성공"은 본문에 대해 아무것도 보증하지 않았습니다.
  - `MpscQueue<SessionPacket>`을 인스턴스화하는 스크래치 TU를 두자 컴파일 에러 2건이 즉시 드러났습니다. 이후 "템플릿은 테스트 먼저"를 규칙으로 삼았습니다.
- **문서 작성 단계에서도 같은 루프**
  - 제출 전 재측정 제안(AI)에 따라 현재 코드로 다시 측정하자 동접 상한이 포트 고갈에서 메모리 예산으로 옮겨 간 것이 드러났습니다. 이전 수치를 그대로 옮겼다면 문서가 코드와 어긋날 뻔했습니다.

### 5.3 공통 원칙

무엇을 만들지 정확히 정의하는 것과 결과를 판단·수정하는 것은 제가 합니다. 전부 맡기면 검증 루프가 무너진다는 것을 경험으로 알고 있어서, 작은 단위로 맡기고 매번 검증하는 방식으로 자리 잡았습니다.

## 6. 구현 현황과 로드맵

네트워크 코어, 스레드 파이프라인(샤드 N=1), DB 자원 안전 계층(RAII)까지 구현되어 있습니다. 그 위의 게임 로직과 운영 기능은 채널 단위 격리 구조를 기준으로 아래 순서로 진행할 계획입니다.  
채널 샤딩과 DB 계층은 "구현됨"과 "로드맵"을 나누어 적었고, 나머지는 전부 로드맵입니다. 로드맵 항목은 설계만 있고 코드는 없습니다.

### 6.1 채널 샤딩 스레드 모델

**구현됨(N=1)**
- `ShardWorker`가 `std::jthread` 1개로 100ms 틱 루프를 돕니다(src/core/shard_worker.cc). `MpscQueue::WaitDrainUntil`로 인바운드를 비우고 `Dispatcher::Dispatch`로 핸들러를 실행한 뒤, 응답이 있으면 주입된 `deliver_`(`ResponseSink`)를 호출합니다.
- `deliver_`의 실체는 src/main.cc의 람다로, 아웃바운드 `MpscQueue`에 넣은 뒤 eventfd에 write해 IO 스레드를 깨웁니다.
- 단일 샤드이므로 세션→샤드 라우팅은 생략했고, IO 스레드는 `HandleSessionEvent`에서 완성 패킷을 곧바로 `ShardWorker::Push`로 넘깁니다.
- 틱 시각 `tick_at`은 매 주기 `tick_at += kTickInterval`(100ms)로 갱신되지만 틱 본문은 아직 비어 있습니다.

**설계**
- 채널 각각을 N개 로직 스레드 중 하나에 고정 배정(샤딩)합니다. 한 채널의 로직은 항상 같은 스레드에서만 실행되므로 채널 내부는 락 없는 직렬 실행이 보장됩니다.
- 락은 IO 스레드와 샤드 스레드의 경계(MPSC 큐, mutex 기반) 한 곳에만 둡니다. 현재 코드에서도 mutex는 그 경계의 인바운드·아웃바운드 `MpscQueue` 두 개에만 있습니다(경계는 하나, 큐는 방향별로 둘).
- 세션 맵(`sessions_`·`id_to_fd_`)과 세션 소켓 쓰기(`Session::Send`·`OnWritable`)는 IO 스레드가 단독 소유합니다. 워커가 쓰는 fd는 깨움용 eventfd뿐입니다. DB 워커가 추가돼도 락은 스레드 경계의 큐에만 둔다는 원칙은 같습니다.

**로드맵**
- 샤드 N=2와 채널→샤드 고정 배정
- 크로스샤드 이동 프로토콜(이동 중 처리 중인 메시지 처리)
- 틱 본문: 하트비트(`kPing`/`kPong`은 `proto::MsgId`에 정의만 되어 있고 핸들러는 미등록)와 30초 무응답 킥

### 6.2 DB 계층

**구현됨**
- `Connection`(`MYSQL*` 소유)과 `Transaction`(`START TRANSACTION`이 성공한 활성 상태에서 커밋 없이 소멸하면 `ROLLBACK`)의 RAII 두 계층
- DB 계층 테스트 5종(이 중 3종이 실 MySQL 필요). 스키마는 `account` 테이블 하나입니다(sql/schema.sql)

**로드맵**
- 접근 구조: 게임 로직은 DB에 직접 붙지 않고 **DB 워커 스레드에 요청을 맡깁니다**(요청에 세션 세대 ID를 함께 담고, 결과는 원래 샤드 큐로 돌려보냄). 커넥션 풀(RAII로 빌리고 반납)과 prepared statement 래퍼도 이 계층에 둡니다
- 안전장치: 데드락(1213) 재시도, 재연결 지수 백오프
- **세대 ID 처리**: DB 응답이 돌아온 시점에 세션이 이미 끊겼을 수 있으므로, 요청 시점의 세대 ID와 비교해 다르면 응답을 버립니다. 비동기 콜백의 수명 문제를 프로토콜로 해결하는 것으로, 출발점은 현재 코드의 세션 ID 처리(`HandleWakeup`에서 `id_to_fd_` 조회 실패 = 죽은 세션의 응답 버림)입니다. 이 버림이 오히려 응답 유실이 되는 half-close 경우는 아래 네트워크 한계 개선의 Draining 상태로 다룹니다
- 시나리오: 로그인(계정 조회) / 던전 클리어 정산 — 골드·경험치·기록을 단일 트랜잭션으로, `SELECT ... FOR UPDATE` 행 잠금과 `action_id` 유니크 제약으로 중복 지급 방지(재시도 안전 = 멱등)

### 6.3 네트워크 한계 개선

**로드맵**(전부 미구현)
- read 루프를 링버퍼 여유 기준으로 중단하고 수신 배압을 TCP 윈도우로 넘길 계획입니다. 현재는 `Session::OnReadable`이 EAGAIN까지 읽어 한 fd가 이벤트 루프를 혼자 차지합니다. 적용하면 수신 킥이 TCP 윈도우 배압으로 대체되고 송신 컷을 항상 같은 결과로 검증할 수 있게 됩니다
- 세션 Draining 상태: FIN 수신 시 즉시 닫지 않고 처리 중인 응답이 나갈 때까지 close를 미뤄 half-close 응답 유실을 해결하는 설계입니다. 던전 소멸 상태와 같은 종류의 문제(비동기 완료 시점과 수명의 경쟁)입니다
- 세션 버퍼 메모리 예산: 현재는 접속 즉시 수신 64KB + 송신 256KB 링을 값-초기화해 세션당 320KB가 커밋되고, 이것이 동접 상한이 됩니다. 송신 링 지연 할당(첫 EAGAIN 때 생성) 또는 링 크기 정책 분리, 메모리 예산의 설정값 노출을 계획합니다
- 미등록 `msg_id`: 반복 시 킥으로 강화할 계획입니다(현재는 로그 후 버림, 연결 유지)

### 6.4 게임 로직 (설계 요약)

**로드맵** — game 계층은 아직 디렉토리가 없습니다. 설계는 다음 순서입니다.
- 로그인: 접속→로그인 요청→계정 조회→응답. 동일 계정 재로그인 시 기존 세션 킥
- 채널: 목록·입장·이동. 채널이 샤드 배정 단위이며, 다른 샤드의 채널로 이동할 때는 위 크로스샤드 이동 프로토콜을 따름
- 던전 인스턴스: Creating/Active/Draining/Destroyed 상태 머신(상태×이벤트 표 먼저), 소멸 중 입장 거부(PendingGuard), **경쟁 GTest** — 소멸과 입장을 같은 틱에 넣어 항상 같은 결과로 재현
- 정산: 클리어 시 위 DB 계층의 정산 트랜잭션 요청·응답. 중복 요청은 1회만 지급되는지 테스트

### 6.5 관측·부하

**로드맵**
- metrics: 세션 수·큐 깊이·DB 지연 카운터의 주기 덤프 / crash_handler: SIGSEGV 시 백트레이스 파일
- 시나리오 봇(접속→로그인→채널→던전 루프) 500세션 측정 — RTT·처리량·큐 깊이·DB 지연. 현재 봇은 echo/drain/bomb 3모드입니다
- 장애 주입: MySQL 강제 종료 후 백오프 재연결 확인, 봇 동시 강제 종료

## 7. 한계

### 7.1 알려진 동작 한계

- **read 루프와 공정성**
  - 현재 구현은 rcvbuf를 커널 하한(요청 1 → 실제 2304바이트)까지 줄여도 수신 컷이 대부분입니다.
  - `OnReadable`의 read 루프가 EAGAIN까지 도는 동안 봇이 커널 수신버퍼를 계속 채워서, 추출이 실행되지 못한 채 수신 링버퍼가 차버리기 때문입니다. 한 fd가 이벤트 루프를 혼자 차지하는 문제이기도 합니다.
- **bomb 다중 세션의 간헐 9/10**
  - `bomb 10`은 간헐적으로 9/10에 그칩니다. 서버 논리 오류가 아니라 컷 판정이 OS 스케줄링에 따라 달라지는 테스트입니다.
  - 근거: 실패 세션이 매 실행 다르고, `bomb 1`은 6회 실행 전부 통과했으며, 모든 실행에서 송신 컷·EPOLLOUT 로그가 없습니다(항상 수신 컷).
  - 수신 컷은 링버퍼(64KB)가 차야 걸리는데, 다중 세션에서는 IO 스레드가 `HandleWakeup`과 다른 세션 이벤트로 분산되고 그 사이 봇이 멈춰 커널 버퍼가 비면 컷을 피합니다. 즉시 에코 구현에서는 read→Send가 한 자리에서 돌아 없던 문제로, 워커 경유로 바뀌며 드러났습니다.
  - 링 여유 기준으로 read를 중단하면 링이 차는 일이 없어져 bomb는 송신 컷으로 항상 같은 결과를 냅니다.
- **half-close 응답 유실**
  - 요청 송신 후 `shutdown(SHUT_WR)`로 응답을 기다리면 `recv`가 0바이트를 돌려줍니다.
  - `read()==0`(FIN)을 전체 종료로 해석해 즉시 세션을 닫기 때문에, 완성 패킷은 워커까지 전달되지만 비동기 응답이 돌아올 때 세션이 이미 닫혀 세션 ID 조회 실패로 버려집니다.
  - 봇 시나리오는 응답을 먼저 읽고 닫으므로 재현되지 않습니다. 해법은 세션 Draining 상태입니다.
- **세션당 320KB 버퍼 선할당과 메모리 상한**
  - 수신 64KB + 송신 256KB 링을 접속 즉시 값-초기화해 커밋하므로 1만 세션에 약 3.2GB(서버 RSS 피크 3,170MB 실측)가 들고, 약 2만 세션에서 OOM으로 종료됩니다.
  - 이전 실측 28,228(클라이언트 임시 포트 고갈)은 송신 링 도입 전 값이라 현재 코드에서는 재현되지 않습니다.

### 7.2 범위와 컷

- 미구현(전부 로드맵): 하트비트, 로그인·중복 킥, 채널 N=2·크로스샤드 이동, 던전 인스턴스·정산, DB 워커·커넥션 풀·prepared statement·세대 ID 처리·데드락 재시도·백오프 재연결, metrics·crash_handler, 시나리오 봇 부하 측정·장애 주입. 이 문서 제출 후에도 개발을 계속하며, 리포에서 진행 상황을 확인하실 수 있습니다
- 테스트 SKIP의 불일치: MySQL 미기동 시 `TransactionTest` 2종은 fixture `SetUp`의 `GTEST_SKIP`으로 건너뛰지만, `ConnectionTest.QueryScalar`는 SKIP 처리가 없어 실패합니다(`Connect`가 `false`를 돌려주는데 `EXPECT_TRUE`로 단언). `ConnectionTest.PrintVersion`·`ConnectWrongPort`는 MySQL 없이 통과합니다
- DB 계층 규칙의 빈틈: `Connection`은 복사 대입 연산자를 delete하지 않았고(현재 대입 사용처는 없음), `mysql_init` 실패를 알리는 접근자가 없습니다. 코드 프리즈 이후 보완 항목입니다
- 부하 실측은 WSL2 단일 머신 루프백 기준이며, 절대 수치보다 상대 비교와 원인 분석에 의미를 두었습니다. 봇은 커넥션을 순차로 돌리므로 동시 요청 처리량은 측정하지 않았습니다
- 프로토콜은 평문이고 인증이 없습니다(로드맵의 로그인도 평문 ID 전제로, 실서비스 인증과 다릅니다). TLS·재접속은 범위에서 제외했습니다
- 미등록 `msg_id`는 로그 후 버리기만 하고 킥하지 않습니다(`Dispatcher::Dispatch`)

## 부록 A. 검증

검증은 세 층으로 나눕니다.
- GTest: 커널 없이 도는 순수 로직(링버퍼·프레이밍·큐·디스패처)과 실 MySQL을 상대하는 DB 계층
- 봇 클라이언트: epoll·부분 write·백프레셔처럼 커널 경로를 포함한 통합 동작
- 수동 실험: 봇 시나리오 밖의 엣지(FIN, 종료 신호)

프리즈 시점 `ctest` 결과는 MySQL 기동 상태에서 26개 전부 통과입니다.

### A.1 단위·통합 테스트 26개

| 스위트 | 개수 | 검증 대상 | 비고 |
|---|---|---|---|
| `SmokeTest` | 1 | `Version()`이 비어 있지 않고 빌드 시 주입된 버전 문자열과 일치 | 빌드·링크·GTest 연결 확인 |
| `RingBufferTest` | 4 | 쓰기 후 `Peek()` 동일 바이트 / 가득 찬 링에 `Write()` 거부(크기 불변) / 소비 후 랩어라운드 쓰기 / `Consume()` 후 `free_space()` 복원 | 소켓 없이 순수 로직 |
| `RingBufferDeathTest` | 1 | `size()`보다 큰 `Consume()`은 `assert`로 즉시 중단(abort, `EXPECT_DEATH`) | 규칙 위반을 조용히 넘기지 않고 즉시 중단(`assert` 기반 — 디버그 프리셋 기준, `NDEBUG` 빌드에서는 비활성) |
| `FramingTest` | 6 | 완성 패킷 추출 / 1바이트씩 넣으며 마지막 바이트 전까지 `kNeedMore` / 3연속 패킷 순서 추출 후 `kNeedMore` / `length`=0은 `kMalformed` / `length`=4097은 `kMalformed` / 링 랩 경계에 걸친 28바이트 패킷 추출 | 악성 `length` 2종은 본문 대기 전에 거부 |
| `MpscQueueTest` | 7 | `Push()` 3건 후 `TryDrain()` 3건 동일 내용 / 빈 큐 0 반환 / 비운 뒤 다시 비우면 0 / 마감 50ms 타임아웃(하한 50ms·상한 2000ms 단언) / 생산자 4스레드 × 1만 건 무손실·무중복·생산자별 순서 보존(전체 상한 5초) / `Push()`가 대기자를 마감 전에 깨움(1초 미만) / `request_stop()`이 대기자를 깨움(1초 미만) | 스레드 테스트도 실패할 수 있게 설계 |
| `DispatcherTest` | 2 | `kEcho` 등록 후 디스패치 → 입력과 동일한 응답 1건 / 미등록 `msg_id` → 응답 없음(로그 후 버림) | 핸들러 테이블 |
| `ConnectionTest` | 3 | 클라이언트 라이브러리 버전 출력 / 실 접속 후 `SELECT 1` == 1 / 잘못된 포트(3307) 접속 실패와 `last_errno()`·`last_error()` 채워짐 | `QueryScalar`만 실 MySQL 필요 |
| `TransactionTest` | 2 | 내부 스코프 탈출(소멸자 `ROLLBACK`) 후 `COUNT(*)` == 0 / `Commit()` 후 `COUNT(*)` == 1 | 픽스처가 접속·정리 담당 |

**통합 테스트 정책 (DB 계층)**
- DB 테스트는 mock 대신 실 MySQL을 상대합니다. `Transaction` 소멸자의 `ROLLBACK`이 실제로 행을 되돌리는지는 실 서버의 `COUNT(*)`로만 확인할 수 있기 때문입니다.
- `TransactionTest` 픽스처는 `Connect` 실패 시 `GTEST_SKIP()`("MySQL 미기동: docker start ejd-mysql")으로 건너뜁니다.
- `SetUp`에서 `user1`·`user2` 행을 미리 `DELETE`한 뒤 커밋해 재실행 가능하게 만듭니다(통과 기준은 "2회 연속 통과").
- 롤백 확인 쿼리는 트랜잭션 객체가 소멸한 뒤(내부 `{ }` 스코프 탈출 후)에 실행합니다.
- 다만 `ConnectionTest.QueryScalar`에는 SKIP 처리가 없어 MySQL 미기동 시 `Connect`의 `EXPECT_TRUE`가 실패합니다. `PrintVersion`·`ConnectWrongPort`는 MySQL이 없어도 통과합니다.

**테스트 원칙**
- 스레드가 얽힌 테스트에는 전체 대기 상한과 실패 단언을 반드시 둡니다(`FourProducersNoLossNoDuplication`은 5초 상한에 `ASSERT_TRUE(now < overall_deadline)`).
- 시간 단언에서 하한은 정확하게, 상한은 넉넉하게 잡습니다(`TimeoutReturnsZeroAtDeadline`은 `EXPECT_GE(elapsed.count(), 50)`·`EXPECT_LE(elapsed.count(), 2000)`).
- 무한 대기는 금지합니다.

### A.2 봇 클라이언트 3모드

봇(`botclient/main.cc`)은 서버(논블로킹)와 달리 블로킹 소켓·`TCP_NODELAY`를 써서 순차 코드로 단순하게 유지합니다.  
사용법은 `./build/debug/ejd_bot [mode] [count]`(mode = `echo`(기본) | `drain` | `bomb`, count 기본 10), 종료 코드는 전부 통과 0 / 하나라도 실패 1입니다.

| 모드 | 서버 인자 | 시나리오 | 통과 기준 | 프리즈 시점 결과 |
|---|---|---|---|---|
| `echo N` | 없음 | 세션 N개가 각각 `"Hello Client: {i}, fd: {fd}"`라는 세션 고유 페이로드를 1회 왕복(`SO_RCVTIMEO` 500ms) | 응답의 `msg_id`와 페이로드가 보낸 것과 일치 → 세션 간 섞임 검출 | `echo ok: 100/100`, 재측정 500/500·10000/10000 |
| `drain` | `1` | 최대 크기(4096바이트) 패킷 4개를 read 없이 전량 송신(16384바이트) 후 전량 수신. 봇도 자기 `SO_RCVBUF`를 4096으로 요청 | 4개 내용 일치 + 서버 로그에 `EPOLLOUT 등록`/`EPOLLOUT 해제` 쌍 | `drain ok: 4/4` |
| `bomb N` | `1 4096` | 세션당 300KB를 read 없이 송신(`MSG_NOSIGNAL`)한 뒤 끊김 대기(`SO_RCVTIMEO` 500ms) | 전 세션이 유한 시간 내 서버에 의해 컷: read가 0(FIN) 또는 `ECONNRESET`(RST)이면 확인, `EAGAIN`(타임아웃)이면 실패 | `bomb ok: 10/10` 3회 반복(간헐 9/10은 알려진 한계) |

- **커널 버퍼 크기 인자.** 루프백의 기본 커널 버퍼는 자동 튜닝으로 수 MB까지 커져 `EAGAIN`이나 버퍼 초과가 재현되지 않습니다. 서버 실행 인자 `./build/debug/ejd_server [sndbuf] [rcvbuf]`(0 또는 생략 = 커널 기본)로 같은 바이너리에서 항상 같은 결과로 재현합니다. 요청값과 실제값은 다릅니다. 요청 1 → 실제 송신 4608바이트 / 수신 2304바이트로, 커널이 하한 보정한 실제 값을 `getsockopt`로 읽어 기동 로그에 출력합니다.
- **컷 판정은 `send` 성공 여부가 아니라 끊김 확인.** 서버가 close해도 봇의 `send`는 봇 커널 송신버퍼로 계속 성공합니다(send 성공 ≠ 전송 완료). 그래서 bomb는 `send` 성공을 증거로 삼지 않고, 송신 중 `EPIPE`/`ECONNRESET`이 오면 그 자체를, 상한까지 보내졌으면 read 대기에서의 0(FIN)/`ECONNRESET`(RST)을 컷 증거로 삼습니다.
- **대량 에코와 fd 누수.** `echo 500` + `echo 10000`(접속 10,500·종료 10,500) 뒤 서버 `/proc/<pid>/fd` 기준 fd 개수는 7 → 7로, 기동 직후와 같습니다.
- **컷 세션과 정상 세션의 격리.** `bomb 3`과 `echo 50`을 동시에 실행해 정상 세션이 영향을 받지 않는지 보는 시나리오의 결과는 3회 실행 전부 `bomb ok: 3/3` + `echo ok: 50/50`입니다(서버 로그 수신 컷 3, 접속 53·종료 53).

### A.3 수동 실험

- **(a) FIN 실험.**
  - `nc -q0`으로 요청 1건과 FIN을 함께 보내면 `Session::OnReadable`이 `read()==0` 뒤에도 링에 남은 완성 패킷을 전부 추출하고, `HandleSessionEvent`가 그 패킷을 워커 큐에 `Push`한 뒤 `CloseSession`을 호출합니다.
  - 워커 도달은 미등록 `msg_id`(99)를 실어 보내 워커의 `미등록 MsgId: msg_id=99, session_id=0` 로그로 확인했습니다. `kEcho` 요청의 응답은 0바이트입니다.
  - `nc -q0`의 FIN은 `shutdown(SHUT_WR)`와 같은 half-close입니다. 데이터와 FIN이 한 번의 `epoll_wait` 반환 안에서 처리되면 서버가 `Push` 직후 `CloseSession`으로 세션을 지워 워커 응답이 버려집니다(0이 아닌 값이 나오는 경우는 데이터와 FIN이 따로 깨운 타이밍 문제). 요청 송신 후 `shutdown(SHUT_WR)`로 응답을 기다리는 실험에서도 `recv`는 0바이트를 돌려줍니다.
- **(b) 종료 실험.**
  - 터미널에서 실행한 서버에 Ctrl+C 또는 `kill -TERM`을 보내면 로그가 `signal 2 수신, 종료 시작`(SIGTERM은 15) → `shutdown: main return` → `shard worker loop exit` 순서로 찍힙니다. 리액터가 먼저 멈추고 워커가 뒤에 멈추는 선언 역순 파괴의 증거입니다.
  - VS Code 디버거(gdb) 안의 Ctrl+C는 신호 전달이 아니라 ptrace 정지라 종료 로그가 나오지 않습니다. 디버거로 실행 중일 때 재현하려면 서버 프로세스에 직접 `kill -INT`를 보내야 합니다.

### A.4 관측 도구

- `ss -tn`: 커널 버퍼에 쌓인 양 확인(서버 행 Send-Q / 봇 행 Recv-Q). Send-Q가 고정돼 있어도 유저스페이스 송신 링버퍼가 커널을 다시 채우는 중일 수 있다는 점을 함께 봐야 합니다.
- `ps -Lo tid,stat,wchan -p <pid>`: 스레드별 상태. `wchan`이 futex면 대기 중이지 스핀이 아닙니다.
- `gdb -batch -ex run -ex "thread apply all bt"`: yama `ptrace_scope` 때문에 attach가 막히면 gdb로 직접 띄워 백트레이스를 얻습니다. SEGV는 `-ex run -ex bt`로 즉시 확인합니다.
- `stdbuf -oL ./build/debug/ejd_server ...`: stdout을 파일로 리다이렉트하면 전량 버퍼링돼 kill 시 로그가 유실되므로 줄 단위 버퍼링으로 우회합니다.

## 부록 B. 빌드 및 실행 (Linux / WSL2, 5분 재현)

요구: Ubuntu 24.04(WSL2), gcc 13+, cmake 3.21+, ninja, pkg-config, libmysqlclient-dev, docker(DB 통합 테스트 전용 — 서버 구동에는 불필요).  
libmysqlclient-dev는 `ejd_lib`가 `pkg_check_modules(mysqlclient)`로 링크하므로 서버 구동에 DB가 없어도 빌드에는 필요합니다. GTest는 FetchContent(v1.18.0)로 받으므로 최초 구성 시 네트워크가 필요합니다.

```bash
cmake --preset debug && cmake --build --preset debug   # 구성·빌드
ctest --preset debug          # 26개 (MySQL 미기동 시 결과는 아래 문단)
```

MySQL이 없으면 TransactionTest 2종은 fixture의 `GTEST_SKIP`으로 SKIP, `ConnectionTest.QueryScalar` 1종은 SKIP 처리가 없어 실패(`Connect`가 false), 나머지 23종은 통과합니다.

MySQL(선택 — DB 테스트 5종을 전부 통과시키려면):

```bash
docker compose up -d --wait   # mysql:8.4, healthy까지 대기
ctest --preset debug          # 26개 전부 통과

# 시드 실패 시에만: 진단 → 볼륨 재생성 또는 수동 시드
docker logs ejd-mysql | grep -E 'running /docker|ERROR'
docker compose down -v && docker compose up -d --wait
docker exec -i ejd-mysql mysql -uroot -p13504 ejd_game < sql/schema.sql
```

컨테이너 `ejd-mysql`, db `ejd_game`, `root`/`13504`는 로컬 개발 전용 크리덴셜로 compose 파일에 평문으로 있습니다.  
`sql/schema.sql`(`account` 테이블)은 볼륨 최초 생성 시에만 initdb로 자동 시드됩니다. 시드가 실패한 반쪽 볼륨(데이터 디렉토리만 생긴 상태)은 재시작해도 initdb가 다시 돌지 않으므로 `down -v`로 볼륨을 지우고 다시 만듭니다.

서버·봇 3모드(상세 시나리오와 통과 기준은 `docs/testing.md`):

```bash
./build/debug/ejd_server &          # 포트 5555. 인자 [sndbuf] [rcvbuf]
./build/debug/ejd_bot echo 100      # → "echo ok: 100/100"
kill -TERM $!

./build/debug/ejd_server 1 &        # SO_SNDBUF 요청 1 → 실제 4608B
./build/debug/ejd_bot drain         # → "drain ok: 4/4" + EPOLLOUT 등록/해제
kill -TERM $!

./build/debug/ejd_server 1 4096 &   # + SO_RCVBUF 축소
./build/debug/ejd_bot bomb 10       # → "bomb ok: 10/10" + 수신버퍼 초과 로그
kill -TERM $!                       # 또는 Ctrl+C
```

- 서버 인자는 0 또는 생략이 커널 기본값이고, 실제 적용된 버퍼 크기는 기동 로그(`getsockopt` 실제 값)로 확인합니다.
- bomb 10은 간헐적으로 9/10에 그치는 알려진 한계가 있습니다.
- 봇의 종료 코드는 연결에 성공한 세션 기준으로 전부 통과하면 0, 하나라도 실패하면 1입니다(스크립트 연쇄용). 출력 N/N의 분모는 요청 수가 아니라 연결 성공 수이므로 서버 미기동 시 echo·bomb는 "0/0"에 종료 코드 0을 내는 알려진 허점이 있고(drain만 연결 실패 시 1), 연결 실패는 `perror` 출력으로만 드러납니다.
- 서버 종료 로그는 "signal N 수신, 종료 시작" → "shutdown: main return" → "shard worker loop exit" 순서로 출력됩니다.
