# ERKillCut — 이터널리턴 킬 하이라이트 자동 컷 편집기

**⬇ 다운로드 (Windows):** [최신 버전 ERKillCut-windows-x64.zip](https://github.com/rhsung1004-ctrl/ErEdits/releases/latest/download/ERKillCut-windows-x64.zip)

방송 다시보기를 넣으면 **킬 장면만 남기고 나머지를 자동으로 잘라** 16:9 영상 한 편으로 만들어 주는 Windows 프로그램입니다.
킬 감지는 [ERShorts](https://github.com/rhsung1004-ctrl/ershorts)와 같은 방식(화면 오른쪽 위 TK·K·A 숫자 변화)을 씁니다.

## 사용 순서
1. **방송 다시보기 열기** (mp4 · mkv · ts · flv … / 창에 끌어 놓아도 됨)
2. **영역 지정** — 방송 화면에서 TK·K·A 숫자 줄에 상자를 맞춤. 확대 화면에 숫자 3개가 초록 상자로 잡히면 성공.
   웹캠·채팅 오버레이로 게임 화면 위치가 다른 스트리머도 상자만 옮기면 됨. **스트리머별 프리셋**으로 저장해 두면 다음부터 그대로 사용.
3. **킬 찾기 시작** — 정확하게(초당 5장) / 빠르게(키프레임만, 킬 시각 ±2초). 분석 결과는 영상별로 자동 저장돼서 다시 열어도 그대로.
4. **클립 확인** — 킬 몇 초 전~후, 연속 킬 합치기 간격, 어시스트 포함 여부.
   클립 더블클릭으로 미리보기, 시작/끝 ±1초 · 현재 위치로 맞추기, 클립 끄기/삭제, 놓친 장면 직접 추가.
5. **자막** (자막 탭) — 재생 위치에 자막 넣기, 여러 줄, 길이·시작/끝 맞추기, 글꼴·크기·글자색·테두리(색/두께)·그림자·반투명 박스·부드럽게 나타나기,
   세로 위치(미리보기에서 자막을 직접 끌어도 됨), "이 모양을 모든 자막에 적용". 마지막에 쓴 모양이 다음 자막의 기본값.
   **글꼴 추가**: TTF/OTF 파일을 골라 추가하면 `%APPDATA%\ERKillCut\fonts`에 복사돼 계속 사용 (미리보기와 결과가 같은 글꼴 파일 사용).
   **스킬 콤보 색**: `Q-F-W-E-R-E-D` 처럼 키를 `-`로 이어 쓰면 키마다 색이 자동으로 바뀜 (기본: QWERD 연한 주황, F 하늘색, `-` 흰색).
   [색 설정…]에서 캐릭터별 프리셋을 만들어 키·색·`-` 색을 바꾸고, 자막마다 프리셋을 고름. 키 하나만 있거나 일반 문장은 그대로.
   자막은 원본 시간 기준으로 저장돼서 클립 길이를 바꿔도 그 장면에 붙어 있음. 사용 클립 밖 자막은 목록에 회색으로 표시.
6. **영상 내보내기** — 클립 사이 **크로스페이드**, 영상 처음 **페이드 인** / 끝 **페이드 아웃** (소리도 함께),
   1080p / 1440p / 720p / 원본 높이, 원본·60·30fps. 그래픽카드 인코더(NVENC → AMF → QSV) 자동 선택, 안 되면 CPU(x264).

## 동작 방식
- 킬 찾기: ffmpeg가 지정 영역만 잘라 160×32 밝기 영상으로 줄여 흘려 보내고, `HudKillWatcher`가 숫자 모양 변화를 봄.
  TK와 K(또는 A)가 3초 안에 함께 바뀌어야 "내가 관여한 킬"로 인정 (팀원 단독 킬 제외). 몇 시간짜리도 메모리에 쌓지 않음.
- 내보내기: 클립마다 `-ss`로 바로 찾아가 읽고, 필터 그래프 하나로 `xfade` + `acrossfade` + 자막(미리보기와 같은 코드로 그린 PNG를 `overlay`) + `fade`/`afade`를 적용해 한 번에 인코딩.

## 프로젝트 구조
```
src/
  HudKillWatcher.*   킬 숫자 변화 감지 (ERShorts와 같은 코드 + 영역 확인용 probe)
  KillScanner.*      ffmpeg로 영상을 훑어 감지기에 넣음 (정확/빠르게, 하드웨어 디코딩 실패 시 CPU로 재시도)
  Segments.*         킬 시점 → 앞뒤 여유를 붙인 클립, 겹치거나 가까운 클립 합치기
  Exporter.*         클립 잘라 붙이기 + 크로스페이드 + 페이드 인/아웃 (ffmpeg 한 번)
  RoiDialog.*        킬 숫자 영역 지정 화면 + 스트리머 프리셋
  TimelineBar.*      원본 전체 타임라인 (클립·킬·자막 표시, 클릭으로 이동)
  Subtitles.*        자막 데이터, 원본 시간 → 결과 시간 변환, 미리보기 그리기
  SubtitlePanel.*    자막 탭 (목록·편집·글꼴 추가)
  ComboColors.*      스킬 콤보 색 프리셋 + 자막 안 콤보 찾기
  ComboDialog.*      캐릭터별 콤보 색 프리셋 편집 화면
  FontManager.*      추가한 글꼴 관리 (ERShorts와 같은 코드)
  VideoView.*        16:9 미리보기 + 자막 표시·끌어서 위치 조정
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
