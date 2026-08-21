# CONVENTIONS.md — eliotjang-dungeon-server 코드 컨벤션

> 근거 문서: `cpp-code-conventions.md` (2026-08-13 결정: **후보 A — Google C++ Style Guide + C++ Core Guidelines 보완**)  
> 이 문서는 리포에서 실제로 지키는 규칙의 요약본.  
> 규칙 변경 시 근거 문서와 이 파일에 함께 기록.

## 1. 네이밍

| 대상 | 규칙 | 예시 |
|---|---|---|
| 파일 | 소문자 snake_case, `.h` / `.cc` | `session_manager.h` |
| 타입 (class/struct/enum/alias) | UpperCamelCase | `class GameSession` |
| 변수 (지역/파라미터) | snake_case | `recv_buffer` |
| 클래스 멤버 변수 | snake_case + trailing `_` | `socket_fd_` |
| struct(POD) 멤버 | trailing `_` 없음 | `pkt.size` |
| 상수 (constexpr/const 전역) | `k` + UpperCamelCase | `kMaxSessions` |
| 함수 | UpperCamelCase | `OnRecv()` |
| 네임스페이스 | 소문자 snake_case | `ejd::net` |
| enum 값 | `enum class` + `k`UpperCamelCase | `PacketType::kLoginReq` |

- 예외: getter/setter처럼 저렴한 접근자는 snake_case (Goggle C++ Style Guide)

## 2. 포맷 — 전부 clang-format으로 자동화

- 기준: `.clang-format` (`BasedOnStyle: Google`, 포인터 좌측 정렬 `T* p`)
- 들여쓰기 2칸, 탭 금지, **줄 길이 80자** (불편해지면 100자 완화 검토 — 변경 시 여기와 근거 문서에 기록)
- 커밋 전 실행: `clang-format -i $(git ls-files '*.h' '*.cc')`
- 검사만: `clang-format --dry-run --Werror <파일...>`

## 3. 언어 기능 규칙 (서버 코드 = `ejd_lib`, `ejd_server`)

- **C++20** (`CMAKE_CXX_STANDARD 20`, `REQUIRED ON`, 컴파일러 확장 OFF)
- **예외 금지** (`-fno-exceptions`) — 에러는 에러코드/`std::expected` 스타일로 반환
- **RTTI 금지** (`-fno-rtti`) — 패킷 디스패치는 타입 ID + 핸들러 테이블로
- **경고는 에러** (`-Wall -Wextra -Werror`)
- **적용 범위**: 위 엄격 옵션은 서버 코드에만 적용한다. `ejd_tests`에는
  `-fno-exceptions`/`-fno-rtti`를 걸지 않는다 — GTest가 내부적으로 예외를 쓸 수
  있고 테스트 코드는 편의가 우선. "예외 금지를 어디까지, 왜"의 경계가
  CMake 타깃 구조(`EJD_STRICT_OPTIONS` 변수)로 그대로 표현되어 있다.
- C 스타일 캐스트 금지 — `static_cast`/`reinterpret_cast` 명시 (버퍼↔구조체 변환 지점을 grep 가능하게)
- 비-trivially-destructible 전역/정적 객체 금지 — 싱글턴 대신 main에서 생성해 주입
- 소유권: `std::unique_ptr` = 소유, raw pointer = 비소유 관찰 (CG R.3 / I.11)
- RAII 필수 (CG R.1), 던지지 않는 함수는 `noexcept` (CG F.6), 멤버는 초기화 리스트 (CG C.49)
- 버퍼 뷰는 `std::span` / 문자열 뷰는 `std::string_view`

## 4. 헤더 규칙

- **include 가드: `#pragma once`** (2026-08-13 확정 — Google은 define 가드지만,
  주요 컴파일러 전부 지원 + 오타/복붙 실수 원천 차단을 이유로 절충 채택)
- 모든 헤더는 self-contained (그 헤더 하나만 include해도 컴파일 가능)
- include 순서: ① 대응 헤더 → ② C 시스템 → ③ C++ 표준 → ④ 외부 라이브러리 → ⑤ 프로젝트 헤더 (그룹 사이 빈 줄)
- include 경로 기준: `src/` (예: `#include "common/version.h"`)

## 5. 주석

- 클래스 주석에 **스레드 안전성 / 동기화 가정을 명시**한다 (멀티스레드 서버에서 최대 실익)
- 주석은 "무엇"이 아니라 "왜"를 설명

## 6. 빌드 타깃 구조 — "코드는 라이브러리, main은 얇게"

| 타깃 | 종류 | 내용 |
|---|---|---|
| `ejd_lib` | STATIC | 모든 서버 코드 (엄격 옵션 적용) |
| `ejd_server` | 실행파일 | `src/main.cc`만 — lib 링크 (엄격 옵션 적용) |
| `ejd_tests` | 실행파일 | `tests/*.cc` — lib + GTest 링크 (엄격 옵션 제외) |

새 서버 코드는 항상 `ejd_lib`에 추가한다 (`CMakeLists.txt`의 `add_library` 소스 목록에 명시적으로 나열 — `file(GLOB)` 금지).

## 7. 빌드/테스트 명령

```bash
cmake --preset debug            # 구성
cmake --build --preset debug    # 빌드
./build/debug/ejd_server        # 실행
ctest --preset debug            # 테스트
```

## 8. 커밋 규칙

> 상세 근거·예시: `git-commit-conventions.md` (프로젝트 공통 레퍼런스). 여기는 이 리포에서 강제하는 요약.

### 메시지 형식 — type 영어 + 제목 한국어 (2026-08-14 확정)

```
<type>: <제목 — 50자 이내, 한국어, 마침표 없음>

<본문(선택) — 코드로 알 수 없는 "왜"만. 72자 줄바꿈>
```

| type | 용도 |
|---|---|
| `feat` | 기능 추가 (서버 동작이 새로 생김) |
| `fix` | 버그 수정 |
| `refactor` | 동작 불변 구조 개선 |
| `test` | 테스트 추가/수정 |
| `build` | 빌드 체계·의존성·도구 (CMake, 프리셋, GTest 도입 등) |
| `docs` | 문서만 변경 |
| `perf` | 성능 개선 — 측정 근거를 본문에 필수 기재 |
| `chore` | 그 외 잡무 (.gitignore 등) |

### 커밋 단위

- **1 커밋 = 1 완결된 의미 변화.** 제목에 "그리고/및"이 필요하면 쪼갠다
- **모든 커밋은 빌드·테스트 통과 상태** (`git bisect` 성립 조건). 커밋 전: `cmake --build --preset debug && ctest --preset debug`
- 포맷팅 전용 변경은 로직과 절대 섞지 않는다 — `chore`/`refactor`로 단독 커밋
- push 전 로컬 WIP 커밋은 정리(squash). push된 히스토리는 rebase/amend 금지

### 금지

- 무의미 제목 금지: `update`, `fix bug`, `수정`, `과제N 작업`
- 비밀값 커밋 금지 — 커밋 전 `git diff --staged` 확인
- 생성물 커밋 금지: `build/`, `compile_commands.json` (.gitignore 선행 정비)
- 저자 정보는 개인 계정(eliotjang)으로 일관 (`git config user.name/user.email` 확인)

제출 시점 태그: `git tag v0.1.0-submit`

## 결정 기록

| 날짜 | 결정 | 근거 |
|---|---|---|
| 2026-08-13 | 컨벤션 A (Google + CG) 채택 | cpp-code-conventions.md 비교 검토 |
| 2026-08-13 | 줄 길이 80자로 시작 | Google 기본값. 불편 시 100자 완화 검토 |
| 2026-08-13 | include 가드 `#pragma once` | 전 컴파일러 지원, 실수 원천 차단 |
| 2026-08-14 | 커밋 언어 A안 — type 영어 + 제목 한국어 | 국내 리뷰어 가독성 + 도구 호환 유지 |
