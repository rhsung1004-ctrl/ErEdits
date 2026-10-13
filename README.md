# ERKillCut — 이터널리턴 킬 하이라이트 자동 컷 편집기

방송 다시보기를 넣으면 **킬 장면만 남기고 나머지를 자동으로 잘라** 16:9 영상 한 편으로 만들어 주는 Windows 프로그램입니다.
킬 감지는 [ERShorts](https://github.com/rhsung1004-ctrl/ershorts)와 같은 방식(화면 오른쪽 위 TK·K·A 숫자 변화)을 씁니다.

## 사용 순서
1. **방송 다시보기 열기** (mp4 · mkv · ts · flv … / 창에 끌어 놓아도 됨)
2. **영역 지정** — 방송 화면에서 TK·K·A 숫자 줄에 상자를 맞춤. 확대 화면에 숫자 3개가 초록 상자로 잡히면 성공.
   웹캠·채팅 오버레이로 게임 화면 위치가 다른 스트리머도 상자만 옮기면 됨. **스트리머별 프리셋**으로 저장해 두면 다음부터 그대로 사용.
3. **킬 찾기 시작** — 정확하게(초당 5장) / 빠르게(키프레임만, 킬 시각 ±2초). 분석 결과는 영상별로 자동 저장돼서 다시 열어도 그대로.
4. **클립 확인** — 킬 몇 초 전~후, 연속 킬 합치기 간격, 어시스트 포함 여부.
   클립 더블클릭으로 미리보기, 시작/끝 ±1초 · 현재 위치로 맞추기, 클립 끄기/삭제, 놓친 장면 직접 추가.
5. **영상 내보내기** — 클립 사이 **크로스페이드**, 영상 처음 **페이드 인** / 끝 **페이드 아웃** (소리도 함께),
   1080p / 1440p / 720p / 원본 높이, 원본·60·30fps. 그래픽카드 인코더(NVENC → AMF → QSV) 자동 선택, 안 되면 CPU(x264).

## 동작 방식
- 킬 찾기: ffmpeg가 지정 영역만 잘라 160×32 밝기 영상으로 줄여 흘려 보내고, `HudKillWatcher`가 숫자 모양 변화를 봄.
  TK와 K(또는 A)가 3초 안에 함께 바뀌어야 "내가 관여한 킬"로 인정 (팀원 단독 킬 제외). 몇 시간짜리도 메모리에 쌓지 않음.
- 내보내기: 클립마다 `-ss`로 바로 찾아가 읽고, 필터 그래프 하나로 `xfade` + `acrossfade` + `fade`/`afade`를 적용해 한 번에 인코딩.

## 프로젝트 구조
```
src/
  HudKillWatcher.*   킬 숫자 변화 감지 (ERShorts와 같은 코드 + 영역 확인용 probe)
  KillScanner.*      ffmpeg로 영상을 훑어 감지기에 넣음 (정확/빠르게, 하드웨어 디코딩 실패 시 CPU로 재시도)
  Segments.*         킬 시점 → 앞뒤 여유를 붙인 클립, 겹치거나 가까운 클립 합치기
  Exporter.*         클립 잘라 붙이기 + 크로스페이드 + 페이드 인/아웃 (ffmpeg 한 번)
  RoiDialog.*        킬 숫자 영역 지정 화면 + 스트리머 프리셋
  TimelineBar.*      원본 전체 타임라인 (클립·킬 표시, 클릭으로 이동)
  MainWindow.*       메인 화면
tests/cli.cpp        화면 없이 엔진만 검사하는 도구 (-DERKILLCUT_TESTS=ON)
```

## exe 만들기
GitHub 저장소에 올리면 `.github/workflows/build-windows.yml`이 Windows 서버에서 빌드해
**ERKillCut-windows-x64.zip**을 만듭니다 (Actions → Artifacts, main 브랜치는 Releases에도 자동 게시).

직접 빌드: Qt 6.8 (Widgets, Multimedia) + CMake + Visual Studio
```
cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH=<Qt 설치 폴더>/msvc2022_64
cmake --build build --config Release
```
실행 폴더에 `ffmpeg.exe`를 함께 두어야 합니다.

## 안내
- 이터널리턴 및 님블뉴런과 관련 없는 **비공식 팬 제작 도구**입니다.
- 라이선스: GNU GPL v3 · 포함 구성요소 고지: THIRD_PARTY_NOTICES.txt
