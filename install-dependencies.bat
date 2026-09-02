@echo off
REM Thin forwarder kept for compatibility: the dependency installer lives in
REM scripts\install-deps.ps1 (the same five idempotent steps: VS Build Tools,
REM CMake + Ninja, LLVM + pinned clang-format, Python + Qt 6.7.3, vcpkg).
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install-deps.ps1" %*
exit /b %ERRORLEVEL%
