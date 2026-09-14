@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "GIT=C:\Program Files\Git\cmd\git.exe"
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
set "CTEST=C:\Program Files\CMake\bin\ctest.exe"
if not exist "%~dp0_claudeout" mkdir "%~dp0_claudeout"
set "LOG=%~dp0_claudeout\run.txt"
echo === claude run %date% %time% === > "%LOG%"
taskkill /F /IM kachakacha_cad.exe > nul 2>&1
taskkill /F /IM kachakacha_cad_next.exe > nul 2>&1
REM Always run GUI executables through a watchdog with a timeout.
REM Without it a modal dialog blocks the watcher forever. This happened once.
set "RUNAPP=powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_RUN_APP.ps1""

echo [1] fetch >> "%LOG%"
REM 未コミットの書きかけ(Codex がこの作業場で作業中のことがある)は
REM **決して stash しない**。オーナー指示 2026-09-13 の項目7。
REM どのファイルが誰の領分かを先に調べ、取り込む差分と当たらないときだけ ff-merge する。
REM 当たるときは BLOCKED_BY_CODEX_WIP を残してここで止める。
if exist "%~dp0_claudeout\BLOCKED_BY_CODEX_WIP.txt" del /q "%~dp0_claudeout\BLOCKED_BY_CODEX_WIP.txt"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0_SAFE_MERGE.ps1" -Repo "%~dp0." -Bundle "%~dp0to-pc.bundle" -Log "%LOG%" >> "%LOG%" 2>&1
set "MERGE_RC=!ERRORLEVEL!"
echo merge_rc=!MERGE_RC! >> "%LOG%"
if "!MERGE_RC!"=="3" (
  echo BLOCKED_BY_CODEX_WIP: local WIP collides. skipping build, test and push. >> "%LOG%"
  echo DONE >> "%LOG%"
  copy /y "%LOG%" "%~dp0_claudeout\latest.txt" > nul
  exit /b 3
)
if "!MERGE_RC!"=="2" (
  echo MERGE_FAILED: nothing merged. cloud-next kept. skipping build and push. >> "%LOG%"
  echo DONE >> "%LOG%"
  copy /y "%LOG%" "%~dp0_claudeout\latest.txt" > nul
  exit /b 2
)
"%GIT%" log --oneline -3 >> "%LOG%" 2>&1
REM ここで見た commit だけがレビュー対象になる。以後 branch が動いても対象は動かない。
for /f "usebackq delims=" %%H in (`"%GIT%" rev-parse HEAD`) do set "REVIEW_HEAD=%%H"
for /f "usebackq delims=" %%B in (`"%GIT%" rev-parse --abbrev-ref HEAD`) do set "TEST_BRANCH=%%B"
echo review_head=!REVIEW_HEAD! >> "%LOG%"

echo [2] configure >> "%LOG%"
"%CMAKE%" --preset windows-msvc > "%~dp0_claudeout\configure.txt" 2>&1
echo configure_rc=!ERRORLEVEL! >> "%LOG%"
findstr /C:"CMake Error" "%~dp0_claudeout\configure.txt" >> "%LOG%" 2>&1

echo [3] build >> "%LOG%"
"%CMAKE%" --build --preset windows-msvc --parallel > "%~dp0_claudeout\build.txt" 2>&1
set "BUILD_RC=!ERRORLEVEL!"
echo build_rc=!BUILD_RC! >> "%LOG%"
findstr /C:": error" "%~dp0_claudeout\build.txt" >> "%LOG%" 2>&1

echo [4] tests >> "%LOG%"
"%CTEST%" --preset windows-msvc > "%~dp0_claudeout\ctest.txt" 2>&1
set "TEST_RC=!ERRORLEVEL!"
type "%~dp0_claudeout\ctest.txt" >> "%LOG%"
echo test_rc=!TEST_RC! >> "%LOG%"
set "CTEST_EVIDENCE="
for /f "usebackq delims=" %%E in (`findstr /C:"tests passed" "%~dp0_claudeout\ctest.txt"`) do set "CTEST_EVIDENCE=%%E"

echo [4b] review pipeline self-test >> "%LOG%"
REM レビュー基盤そのものの検査。製品のテストとは別に、毎回ここで動かす。
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\ai-local\review-selftest.ps1" >> "%LOG%" 2>&1
set "REVIEW_SELFTEST_RC=!ERRORLEVEL!"
echo review_selftest_rc=!REVIEW_SELFTEST_RC! >> "%LOG%"

echo [5] snapshots >> "%LOG%"
set "EXE=%~dp0build-msvc2022-x64\Release\kachakacha_cad_next.exe"
if exist "%EXE%" (
  for %%S in (empty grid tools curves curves-win95 draw-line snap isometric win95 guide mode-part mode-fabrication mode-output view-cube guide-table cursor-input active-group steps-part steps-fabrication steps-output select export sample) do (
    %RUNAPP% 120 "%EXE%" --manual-state %%S --snapshot "%~dp0_claudeout\v2-%%S.png" >> "%LOG%" 2>&1
    echo shot_%%S=!ERRORLEVEL! >> "%LOG%"
    taskkill /F /IM kachakacha_cad_next.exe > nul 2>&1
  )
  REM WP-12: write the sample, open it for real, and take a snapshot.
  "%~dp0build-msvc2022-x64\Release\kachakacha_v2_write_sample.exe" "%~dp0samples\v2-sample.kcd2" >> "%LOG%" 2>&1
  echo write_sample=!ERRORLEVEL! >> "%LOG%"
  %RUNAPP% 120 "%EXE%" --open "%~dp0samples\v2-sample.kcd2" --snapshot "%~dp0_claudeout\v2-opened-sample.png" >> "%LOG%" 2>&1
  echo open_sample=!ERRORLEVEL! >> "%LOG%"
  taskkill /F /IM kachakacha_cad_next.exe > nul 2>&1

  REM AT-UIX-010 Theme/DPI: same state at 2 window sizes x 4 scale factors.
  if not exist "%~dp0_claudeout\dpi" mkdir "%~dp0_claudeout\dpi"
  for %%Z in (1366x768 1920x1080) do (
    for %%T in (guide-table cursor-input) do (
      for %%D in (1 1.25 1.5 2) do (
        set "QT_SCALE_FACTOR=%%D"
        %RUNAPP% 120 "%EXE%" --manual-state %%T --size %%Z --snapshot "%~dp0_claudeout\dpi\v2-%%T-%%Z-%%D.png" >> "%LOG%" 2>&1
        echo shot_dpi_%%T_%%Z_%%D=!ERRORLEVEL! >> "%LOG%"
        taskkill /F /IM kachakacha_cad_next.exe > nul 2>&1
      )
    )
  )
  set "QT_SCALE_FACTOR="
  for %%Z in (1366x768 1920x1080) do (
    %RUNAPP% 120 "%EXE%" --manual-state curves-win95 --size %%Z --snapshot "%~dp0_claudeout\dpi\v2-win95-%%Z.png" >> "%LOG%" 2>&1
    echo shot_dpi_win95_%%Z=!ERRORLEVEL! >> "%LOG%"
    taskkill /F /IM kachakacha_cad_next.exe > nul 2>&1
  )
  %RUNAPP% 600 "%EXE%" --self-test >> "%LOG%" 2>&1
  set "APP_SELFTEST_RC=!ERRORLEVEL!"
  echo selftest_rc=!APP_SELFTEST_RC! >> "%LOG%"
  taskkill /F /IM kachakacha_cad_next.exe > nul 2>&1
) else (
  echo no cad_next exe >> "%LOG%"
)

echo [6] push >> "%LOG%"
REM push が上げるのはコミット済みのものだけである。書きかけは上がらない。
REM だから書きかけがあっても push は止めない。止めると雲の成果が永久に届かない。
REM 書きかけを守るのは [1] の取り込みの側の仕事である。
"%GIT%" update-index -q --refresh > nul 2>&1
"%GIT%" diff --quiet HEAD > nul 2>&1
if not "!ERRORLEVEL!"=="0" echo note=local_wip_present_push_is_commit_only >> "%LOG%"
if "!BUILD_RC!"=="0" if "!TEST_RC!"=="0" (
  "%GIT%" push origin HEAD:codex/v2-wp01-build-scaffold >> "%LOG%" 2>&1
  echo push_rc=!ERRORLEVEL! >> "%LOG%"
)
if not "!BUILD_RC!!TEST_RC!"=="00" (
  echo push_skipped build_rc=!BUILD_RC! test_rc=!TEST_RC! >> "%LOG%"
)
echo [7] launch >> "%LOG%"
REM The owner asked to have the app running after every build.
REM Start it detached so this script can finish; the watcher must not wait on it.
if exist "%EXE%" (
  if "!BUILD_RC!"=="0" (
    start "" "%EXE%"
    echo launched=%EXE% >> "%LOG%"
  ) else (
    echo launch_skipped build_rc=!BUILD_RC! >> "%LOG%"
  )
)

echo [8] review queue >> "%LOG%"
REM ここが Codex を起こす唯一の場所である。
REM ビルドとテストが本当に通り、ビルドした commit と今の HEAD が同じときだけ依頼が積まれる。
REM 依頼(tools\ai-local\next-review.json)が無ければ何も起きない。Codex に仕事を探させない。
for /f "usebackq delims=" %%H in (`"%GIT%" rev-parse HEAD`) do set "TESTED_HEAD=%%H"
echo tested_head=!TESTED_HEAD! >> "%LOG%"
set "BUILD_WORD=FAIL"
if "!BUILD_RC!"=="0" set "BUILD_WORD=PASS"
set "TEST_WORD=FAIL"
if "!TEST_RC!"=="0" set "TEST_WORD=PASS"
set "APP_WORD=SKIPPED"
if defined APP_SELFTEST_RC (
  set "APP_WORD=FAIL"
  if "!APP_SELFTEST_RC!"=="0" set "APP_WORD=PASS"
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\ai-local\review-enqueue.ps1" -RepoRoot "%~dp0." -ReviewCommit "!REVIEW_HEAD!" -TestedCommit "!TESTED_HEAD!" -BuildResult "!BUILD_WORD!" -TestResult "!TEST_WORD!" -SelfTestResult "!APP_WORD!" -Branch "!TEST_BRANCH!" -CtestEvidence "!CTEST_EVIDENCE!" -LogPath "_claudeout/run.txt" >> "%LOG%" 2>&1
echo enqueue_rc=!ERRORLEVEL! >> "%LOG%"

REM 古い版のまま居座っている見張りだけを引き取らせる。
REM 今の版で動いているものには触らない(レビュー中かもしれない)。
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\ai-local\stop-stale-dispatcher.ps1" -RepoRoot "%~dp0." >> "%LOG%" 2>&1

REM 常駐の見張りを起こす。既に動いていれば二つ目は自分で黙って終わる。
start "" /min powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\ai-local\review-dispatcher.ps1" -RepoRoot "%~dp0."
REM 見張りが鍵を掴むまで少し待つ。待たずに数えると「動いていない」と出る。
timeout /t 5 /nobreak > nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\ai-local\queue-status.ps1" -RepoRoot "%~dp0." >> "%LOG%" 2>&1

echo DONE >> "%LOG%"
copy /y "%LOG%" "%~dp0_claudeout\latest.txt" > nul
