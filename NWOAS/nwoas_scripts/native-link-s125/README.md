# S125 설치된 Windows용 통신 수신기 후보

S123 WinPE 수신기와 분리한 NWOS.EXE. SystemDrive C:에서만 시작하며 동일한 host RAM 가상 디스크 식별/세션 토큰 확인을 거쳐 작업을 받는다. 임시 CMD는 C:\Windows\Temp\NWOAS123.CMD. 네트워크 사용 없음.

빌드와 API stub 검사는 통과했으나 설치된 Windows에서 아직 실행하지 않았다. 현재 WinPE 자동 시작 경로와 NWAGENT.EXE는 변경하지 않았다. 사용 시 사용자 소유 맥미니의 임시 개발 통신 용도로만 배치한다.
